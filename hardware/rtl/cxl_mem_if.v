// hardware/rtl/cxl_mem_if.v
// CXL.mem and CXL.cache interface wrapper
// Provides cache-coherent memory access to CXL memory pool

module cxl_mem_if #(
    parameter ADDR_WIDTH = 64,
    parameter DATA_WIDTH = 512,
    parameter CACHE_LINE_SIZE = 64
)(
    input  wire                        clk,
    input  wire                        rst_n,
    
    // Internal interface (from DMA engine / prefetch core)
    input  wire                        req_valid,
    output wire                        req_ready,
    input  wire                        req_write,
    input  wire [ADDR_WIDTH-1:0]       req_addr,
    input  wire [DATA_WIDTH-1:0]      req_data,
    input  wire [DATA_WIDTH/8-1:0]    req_strb,
    input  wire [7:0]                 req_len,
    
    output reg                         resp_valid,
    input  wire                        resp_ready,
    output reg  [DATA_WIDTH-1:0]      resp_data,
    output reg                         resp_last,
    
    // CXL.mem interface (simplified - real implementation uses CXL IP)
    output reg                         cxl_mem_req_valid,
    input  wire                        cxl_mem_req_ready,
    output reg                         cxl_mem_req_write,
    output reg  [ADDR_WIDTH-1:0]       cxl_mem_req_addr,
    output reg  [DATA_WIDTH-1:0]      cxl_mem_req_data,
    output reg  [DATA_WIDTH/8-1:0]     cxl_mem_req_strb,
    
    input  wire                        cxl_mem_resp_valid,
    output reg                         cxl_mem_resp_ready,
    input  wire [DATA_WIDTH-1:0]       cxl_mem_resp_data,
    
    // CXL.cache coherence interface
    output reg                         cxl_cache_inv_valid,
    input  wire                        cxl_cache_inv_ready,
    output reg  [ADDR_WIDTH-1:0]       cxl_cache_inv_addr
);

    // Request buffer
    reg req_pending;
    reg [ADDR_WIDTH-1:0] pending_addr;
    reg [7:0] pending_len;
    reg [7:0] burst_count;
    
    assign req_ready = !req_pending;
    
    // Request handling
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            req_pending <= 1'b0;
            burst_count <= 0;
            cxl_mem_req_valid <= 1'b0;
            cxl_mem_resp_ready <= 1'b0;
            resp_valid <= 1'b0;
        end else begin
            // Accept new request
            if (req_valid && req_ready) begin
                req_pending <= 1'b1;
                pending_addr <= req_addr;
                pending_len <= req_len;
                burst_count <= 0;
                
                // Issue CXL request
                cxl_mem_req_valid <= 1'b1;
                cxl_mem_req_write <= req_write;
                cxl_mem_req_addr <= req_addr;
                cxl_mem_req_data <= req_data;
                cxl_mem_req_strb <= req_strb;
            end
            
            // Handle CXL request ready
            if (cxl_mem_req_valid && cxl_mem_req_ready) begin
                cxl_mem_req_valid <= 1'b0;
            end
            
            // Handle CXL response
            if (cxl_mem_resp_valid && cxl_mem_resp_ready) begin
                resp_valid <= 1'b1;
                resp_data <= cxl_mem_resp_data;
                resp_last <= (burst_count == pending_len);
                
                if (burst_count < pending_len) begin
                    burst_count <= burst_count + 1;
                    // Issue next request in burst
                    if (burst_count < pending_len) begin
                        cxl_mem_req_valid <= 1'b1;
                        cxl_mem_req_addr <= pending_addr + (burst_count + 1) * CACHE_LINE_SIZE;
                    end
                end else begin
                    req_pending <= 1'b0;
                end
            end
            
            // Response ready
            if (resp_valid && resp_ready) begin
                resp_valid <= 1'b0;
                cxl_mem_resp_ready <= 1'b1;
            end else begin
                cxl_mem_resp_ready <= 1'b0;
            end
            
            // Cache invalidation (for writes)
            if (req_valid && req_ready && req_write) begin
                cxl_cache_inv_valid <= 1'b1;
                cxl_cache_inv_addr <= req_addr;
            end
            
            if (cxl_cache_inv_valid && cxl_cache_inv_ready) begin
                cxl_cache_inv_valid <= 1'b0;
            end
        end
    end

endmodule

