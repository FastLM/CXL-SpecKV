// hardware/rtl/cxl_speckv_top.v
// Top-level module integrating all components

module cxl_speckv_top (
    input  wire                        clk,
    input  wire                        rst_n,
    
    // MMIO interface (from host)
    input  wire [31:0]                 mmio_addr,
    input  wire                        mmio_wr_en,
    input  wire                        mmio_rd_en,
    input  wire [63:0]                 mmio_wr_data,
    output reg  [63:0]                 mmio_rd_data,
    
    // CXL.mem interface
    // (Connect to CXL IP core)
    
    // AXI interface to HBM
    // (Connect to HBM controller)
    
    // Status/control
    output wire [31:0]                 status_reg,
    output wire [31:0]                 completion_count
);

    // Instantiate components
    // ATU
    wire atu_req_valid, atu_req_ready;
    wire [63:0] atu_req_vaddr;
    wire atu_resp_valid, atu_resp_ready;
    wire [63:0] atu_resp_paddr;
    wire atu_resp_hit;
    
    atu #(
        .VADDR_WIDTH(64),
        .PADDR_WIDTH(64),
        .TLB_ENTRIES(256)
    ) u_atu (
        .clk(clk),
        .rst_n(rst_n),
        .req_valid(atu_req_valid),
        .req_vaddr(atu_req_vaddr),
        .req_ready(atu_req_ready),
        .resp_valid(atu_resp_valid),
        .resp_paddr(atu_resp_paddr),
        .resp_hit(atu_resp_hit),
        .resp_ready(atu_resp_ready),
        .pt_rd_valid(),
        .pt_rd_vaddr(),
        .pt_rd_ready(1'b1),
        .pt_rd_data_valid(1'b0),
        .pt_rd_paddr(64'd0)
    );
    
    // Compression pipeline
    wire comp_s_axis_tvalid, comp_s_axis_tready;
    wire [16*128-1:0] comp_s_axis_tdata;
    wire comp_m_axis_tvalid, comp_m_axis_tready;
    wire [8*128-1:0] comp_m_axis_tdata;
    wire [15:0] comp_m_axis_tscale;
    
    kv_compress #(
        .N(1024),
        .D(128),
        .IN_W(16),
        .OUT_W(8)
    ) u_compress (
        .clk(clk),
        .rst_n(rst_n),
        .s_axis_tvalid(comp_s_axis_tvalid),
        .s_axis_tready(comp_s_axis_tready),
        .s_axis_tdata(comp_s_axis_tdata),
        .s_axis_tlast(1'b0),
        .m_axis_tvalid(comp_m_axis_tvalid),
        .m_axis_tready(comp_m_axis_tready),
        .m_axis_tdata(comp_m_axis_tdata),
        .m_axis_tscale(comp_m_axis_tscale),
        .m_axis_tlast()
    );
    
    // Decompression pipeline
    wire decomp_s_axis_tvalid, decomp_s_axis_tready;
    wire [8*128-1:0] decomp_s_axis_tdata;
    wire [15:0] decomp_s_axis_tscale;
    wire decomp_m_axis_tvalid, decomp_m_axis_tready;
    wire [16*128-1:0] decomp_m_axis_tdata;
    
    kv_decompress #(
        .N(1024),
        .D(128),
        .IN_W(8),
        .OUT_W(16)
    ) u_decompress (
        .clk(clk),
        .rst_n(rst_n),
        .s_axis_tvalid(decomp_s_axis_tvalid),
        .s_axis_tready(decomp_s_axis_tready),
        .s_axis_tdata(decomp_s_axis_tdata),
        .s_axis_tscale(decomp_s_axis_tscale),
        .s_axis_tlast(1'b0),
        .m_axis_tvalid(decomp_m_axis_tvalid),
        .m_axis_tready(decomp_m_axis_tready),
        .m_axis_tdata(decomp_m_axis_tdata),
        .m_axis_tlast()
    );
    
    // DMA engine
    wire dma_desc_valid, dma_desc_ready;
    wire [127:0] dma_desc_data;
    wire dma_done_valid;
    wire [7:0] dma_done_count;
    
    dma_engine #(
        .DESC_WIDTH(128),
        .ADDR_WIDTH(64),
        .DATA_WIDTH(512)
    ) u_dma (
        .clk(clk),
        .rst_n(rst_n),
        .desc_valid(dma_desc_valid),
        .desc_ready(dma_desc_ready),
        .desc_data(dma_desc_data),
        // AXI interfaces (connect to CXL/HBM)
        .m_axi_arvalid(),
        .m_axi_arready(1'b0),
        .m_axi_araddr(),
        .m_axi_arlen(),
        .m_axi_arsize(),
        .m_axi_arburst(),
        .m_axi_rvalid(1'b0),
        .m_axi_rready(),
        .m_axi_rdata(512'd0),
        .m_axi_rlast(1'b0),
        .m_axi_awvalid(),
        .m_axi_awready(1'b0),
        .m_axi_awaddr(),
        .m_axi_awlen(),
        .m_axi_awsize(),
        .m_axi_awburst(),
        .m_axi_wvalid(),
        .m_axi_wready(1'b0),
        .m_axi_wdata(),
        .m_axi_wstrb(),
        .m_axi_wlast(),
        .m_axi_bvalid(1'b0),
        .m_axi_bready(),
        .done_valid(dma_done_valid),
        .done_count(dma_done_count)
    );
    
    // Prefetch core
    wire prefetch_req_valid, prefetch_req_ready;
    wire [31:0] prefetch_req_id;
    wire [15:0] prefetch_req_layer;
    wire [31:0] prefetch_req_pos;
    wire [7:0] prefetch_req_depth_k;
    wire [32*16-1:0] prefetch_req_tokens;
    
    prefetch_core #(
        .REQ_ID_WIDTH(32),
        .LAYER_WIDTH(16),
        .POS_WIDTH(32),
        .TOKEN_WIDTH(32),
        .HISTORY_LEN(16),
        .DEPTH_K(4)
    ) u_prefetch (
        .clk(clk),
        .rst_n(rst_n),
        .prefetch_req_valid(prefetch_req_valid),
        .prefetch_req_ready(prefetch_req_ready),
        .prefetch_req_id(prefetch_req_id),
        .prefetch_req_layer(prefetch_req_layer),
        .prefetch_req_pos(prefetch_req_pos),
        .prefetch_req_depth_k(prefetch_req_depth_k),
        .prefetch_req_tokens(prefetch_req_tokens),
        // LSTM interface (connect to LSTM block)
        .lstm_req_valid(),
        .lstm_req_ready(1'b0),
        .lstm_req_tokens(),
        .lstm_resp_valid(1'b0),
        .lstm_resp_ready(),
        .lstm_resp_tokens(128'd0),
        .lstm_resp_confidences(32'd0),
        // ATU interface
        .atu_req_valid(atu_req_valid),
        .atu_req_ready(atu_req_ready),
        .atu_req_vaddr(atu_req_vaddr),
        .atu_resp_valid(atu_resp_valid),
        .atu_resp_ready(atu_resp_ready),
        .atu_resp_paddr(atu_resp_paddr),
        .atu_resp_hit(atu_resp_hit),
        // DMA interface
        .dma_desc_valid(dma_desc_valid),
        .dma_desc_ready(dma_desc_ready),
        .dma_desc_data(dma_desc_data),
        // Directory interface
        .dir_check_valid(),
        .dir_check_ready(1'b0),
        .dir_check_addr(),
        .dir_check_resp_valid(1'b0),
        .dir_check_resp_ready(),
        .dir_check_hit(1'b0),
        .dir_check_tier(2'd0)
    );
    
    // MMIO register interface
    reg [31:0] status;
    reg [31:0] completion_cnt;
    
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            status <= 32'd0;
            completion_cnt <= 32'd0;
        end else begin
            // MMIO write
            if (mmio_wr_en) begin
                case (mmio_addr[15:0])
                    16'h0000: status <= mmio_wr_data[31:0];
                    // Add more registers as needed
                endcase
            end
            
            // MMIO read
            if (mmio_rd_en) begin
                case (mmio_addr[15:0])
                    16'h0000: mmio_rd_data <= {32'd0, status};
                    16'h0004: mmio_rd_data <= {32'd0, completion_cnt};
                    default: mmio_rd_data <= 64'd0;
                endcase
            end
            
            // Update completion count
            if (dma_done_valid) begin
                completion_cnt <= completion_cnt + dma_done_count;
            end
        end
    end
    
    assign status_reg = status;
    assign completion_count = completion_cnt;

endmodule

