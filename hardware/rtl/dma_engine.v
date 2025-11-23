// hardware/rtl/dma_engine.v
// DMA Engine for CXL memory transfers
// Handles scatter-gather DMA operations with compression support

module dma_engine #(
    parameter DESC_WIDTH = 128,
    parameter ADDR_WIDTH = 64,
    parameter DATA_WIDTH = 512,
    parameter MAX_DESC = 16,
    parameter DESC_FIFO_DEPTH = 32
)(
    input  wire                        clk,
    input  wire                        rst_n,
    
    // Descriptor FIFO interface (from prefetch_core / host MMIO)
    input  wire                        desc_valid,
    output wire                        desc_ready,
    input  wire [DESC_WIDTH-1:0]      desc_data,
    
    // AXI Master interface to CXL.mem (FPGA HBM)
    // Read channel
    output reg                         m_axi_arvalid,
    input  wire                        m_axi_arready,
    output reg  [ADDR_WIDTH-1:0]       m_axi_araddr,
    output reg  [7:0]                  m_axi_arlen,
    output reg  [2:0]                  m_axi_arsize,
    output reg  [1:0]                  m_axi_arburst,
    
    input  wire                        m_axi_rvalid,
    output reg                         m_axi_rready,
    input  wire [DATA_WIDTH-1:0]       m_axi_rdata,
    input  wire                        m_axi_rlast,
    
    // Write channel
    output reg                         m_axi_awvalid,
    input  wire                        m_axi_awready,
    output reg  [ADDR_WIDTH-1:0]      m_axi_awaddr,
    output reg  [7:0]                 m_axi_awlen,
    output reg  [2:0]                 m_axi_awsize,
    output reg  [1:0]                 m_axi_awburst,
    
    output reg                         m_axi_wvalid,
    input  wire                        m_axi_wready,
    output reg  [DATA_WIDTH-1:0]      m_axi_wdata,
    output reg  [DATA_WIDTH/8-1:0]     m_axi_wstrb,
    output reg                         m_axi_wlast,
    
    input  wire                        m_axi_bvalid,
    output reg                         m_axi_bready,
    
    // Completion interface
    output reg                         done_valid,
    output reg  [7:0]                  done_count
);

    // Descriptor structure
    typedef struct packed {
        logic [63:0] fpga_addr;
        logic [63:0] gpu_addr;
        logic [31:0] bytes;
        logic [7:0]  flags;  // bit0: RD/WR, bit1: COMPRESSED, bit2: PREFETCH
    } dma_desc_t;
    
    // Descriptor FIFO
    dma_desc_t [DESC_FIFO_DEPTH-1:0] desc_fifo;
    reg [5:0] desc_wr_ptr, desc_rd_ptr;
    reg [5:0] desc_count;
    
    // Current descriptor
    dma_desc_t curr_desc;
    reg [31:0] bytes_remaining;
    reg [ADDR_WIDTH-1:0] curr_addr;
    
    // State machine
    typedef enum logic [2:0] {
        IDLE,
        READ_DESC,
        READ_DATA,
        WRITE_DATA,
        WAIT_COMPLETE
    } state_t;
    
    state_t state, next_state;
    
    // FIFO management
    assign desc_ready = (desc_count < DESC_FIFO_DEPTH);
    
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            desc_wr_ptr <= 0;
            desc_rd_ptr <= 0;
            desc_count <= 0;
        end else begin
            // Write to FIFO
            if (desc_valid && desc_ready) begin
                desc_fifo[desc_wr_ptr] <= desc_data;
                desc_wr_ptr <= desc_wr_ptr + 1;
                desc_count <= desc_count + 1;
            end
            
            // Read from FIFO
            if ((state == IDLE || state == READ_DESC) && desc_count > 0) begin
                curr_desc <= desc_fifo[desc_rd_ptr];
                desc_rd_ptr <= desc_rd_ptr + 1;
                desc_count <= desc_count - 1;
            end
        end
    end
    
    // State machine
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE;
            bytes_remaining <= 0;
            curr_addr <= 0;
            done_count <= 0;
        end else begin
            state <= next_state;
            
            case (state)
                READ_DESC: begin
                    bytes_remaining <= curr_desc.bytes;
                    curr_addr <= curr_desc.fpga_addr;
                end
                
                READ_DATA: begin
                    if (m_axi_rvalid && m_axi_rready) begin
                        bytes_remaining <= bytes_remaining - (DATA_WIDTH/8);
                        curr_addr <= curr_addr + (DATA_WIDTH/8);
                    end
                end
                
                WRITE_DATA: begin
                    if (m_axi_wvalid && m_axi_wready) begin
                        bytes_remaining <= bytes_remaining - (DATA_WIDTH/8);
                        curr_addr <= curr_addr + (DATA_WIDTH/8);
                    end
                end
                
                WAIT_COMPLETE: begin
                    if (m_axi_bvalid && m_axi_bready) begin
                        done_count <= done_count + 1;
                    end
                end
            endcase
        end
    end
    
    // State machine logic
    always_comb begin
        next_state = state;
        
        m_axi_arvalid = 1'b0;
        m_axi_awvalid = 1'b0;
        m_axi_wvalid = 1'b0;
        m_axi_rready = 1'b0;
        m_axi_bready = 1'b0;
        m_axi_wlast = 1'b0;
        done_valid = 1'b0;
        
        case (state)
            IDLE: begin
                if (desc_count > 0) begin
                    next_state = READ_DESC;
                end
            end
            
            READ_DESC: begin
                if (curr_desc.flags[0]) begin  // Read
                    next_state = READ_DATA;
                end else begin  // Write
                    next_state = WRITE_DATA;
                end
            end
            
            READ_DATA: begin
                m_axi_arvalid = 1'b1;
                m_axi_araddr = curr_addr;
                m_axi_arlen = (bytes_remaining > DATA_WIDTH/8) ? 8'd15 : (bytes_remaining / (DATA_WIDTH/8) - 1);
                m_axi_arsize = 3'b110;  // 64 bytes
                m_axi_arburst = 2'b01;  // INCR
                
                m_axi_rready = 1'b1;
                
                if (m_axi_rvalid && m_axi_rready && m_axi_rlast) begin
                    if (bytes_remaining <= DATA_WIDTH/8) begin
                        next_state = WAIT_COMPLETE;
                    end
                end
            end
            
            WRITE_DATA: begin
                m_axi_awvalid = 1'b1;
                m_axi_awaddr = curr_addr;
                m_axi_awlen = (bytes_remaining > DATA_WIDTH/8) ? 8'd15 : (bytes_remaining / (DATA_WIDTH/8) - 1);
                m_axi_awsize = 3'b110;
                m_axi_awburst = 2'b01;
                
                m_axi_wvalid = 1'b1;
                m_axi_wdata = m_axi_rdata;  // Simplified - would come from compression pipeline
                m_axi_wstrb = {(DATA_WIDTH/8){1'b1}};
                m_axi_wlast = (bytes_remaining <= DATA_WIDTH/8);
                
                if (m_axi_wvalid && m_axi_wready && m_axi_wlast) begin
                    next_state = WAIT_COMPLETE;
                end
            end
            
            WAIT_COMPLETE: begin
                m_axi_bready = 1'b1;
                if (m_axi_bvalid && m_axi_bready) begin
                    done_valid = 1'b1;
                    next_state = IDLE;
                end
            end
        endcase
    end

endmodule

