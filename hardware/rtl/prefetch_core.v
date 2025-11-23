// hardware/rtl/prefetch_core.v
// Speculative Prefetch Core (Algorithm 1 from paper)
// Coordinates LSTM prediction and DMA prefetch requests

module prefetch_core #(
    parameter REQ_ID_WIDTH = 32,
    parameter LAYER_WIDTH = 16,
    parameter POS_WIDTH = 32,
    parameter TOKEN_WIDTH = 32,
    parameter HISTORY_LEN = 16,
    parameter DEPTH_K = 4,
    parameter DESC_WIDTH = 128
)(
    input  wire                        clk,
    input  wire                        rst_n,
    
    // Prefetch request interface (from host via MMIO)
    input  wire                        prefetch_req_valid,
    output wire                        prefetch_req_ready,
    input  wire [REQ_ID_WIDTH-1:0]     prefetch_req_id,
    input  wire [LAYER_WIDTH-1:0]     prefetch_req_layer,
    input  wire [POS_WIDTH-1:0]       prefetch_req_pos,
    input  wire [7:0]                 prefetch_req_depth_k,
    input  wire [TOKEN_WIDTH*HISTORY_LEN-1:0] prefetch_req_tokens,
    
    // LSTM prediction interface
    output reg                         lstm_req_valid,
    input  wire                        lstm_req_ready,
    output reg  [TOKEN_WIDTH*HISTORY_LEN-1:0] lstm_req_tokens,
    
    input  wire                        lstm_resp_valid,
    output reg                         lstm_resp_ready,
    input  wire [TOKEN_WIDTH*DEPTH_K-1:0] lstm_resp_tokens,  // Top-k predictions
    input  wire [7:0]                 lstm_resp_confidences,
    
    // ATU interface (for address translation)
    output reg                         atu_req_valid,
    input  wire                        atu_req_ready,
    output reg  [64-1:0]              atu_req_vaddr,
    
    input  wire                        atu_resp_valid,
    output reg                         atu_resp_ready,
    input  wire [64-1:0]              atu_resp_paddr,
    input  wire                        atu_resp_hit,
    
    // DMA descriptor output
    output reg                         dma_desc_valid,
    input  wire                        dma_desc_ready,
    output reg  [DESC_WIDTH-1:0]       dma_desc_data,
    
    // L1/L2 directory (check if already cached)
    output reg                         dir_check_valid,
    input  wire                        dir_check_ready,
    output reg  [64-1:0]              dir_check_addr,
    
    input  wire                        dir_check_resp_valid,
    output reg                         dir_check_resp_ready,
    input  wire                        dir_check_hit,  // 1=in L1/L2
    input  wire [1:0]                 dir_check_tier  // 0=L1, 1=L2, 2=L3
);

    // State machine
    typedef enum logic [2:0] {
        IDLE,
        REQUEST_LSTM,
        WAIT_LSTM,
        TRANSLATE_ADDR,
        CHECK_DIR,
        ISSUE_DMA,
        DONE
    } state_t;
    
    state_t state, next_state;
    
    // Request storage
    reg [REQ_ID_WIDTH-1:0] req_id;
    reg [LAYER_WIDTH-1:0] req_layer;
    reg [POS_WIDTH-1:0] req_pos;
    reg [7:0] req_depth_k;
    reg [TOKEN_WIDTH*HISTORY_LEN-1:0] req_tokens;
    
    // Prediction storage
    reg [TOKEN_WIDTH*DEPTH_K-1:0] pred_tokens;
    reg [7:0] pred_confidences [DEPTH_K-1:0];
    
    // Prefetch iteration
    reg [7:0] prefetch_idx;
    reg [64-1:0] virt_addr;
    reg [64-1:0] phys_addr;
    
    // Address encoding helper
    function [63:0] encode_virt_addr(
        input [REQ_ID_WIDTH-1:0] r_id,
        input [LAYER_WIDTH-1:0] layer,
        input [POS_WIDTH-1:0] pos
    );
        encode_virt_addr = {r_id, layer, 8'd0, pos, 1'b0};  // [req:32][layer:16][head:8][pos:32][kind:1]
    endfunction
    
    // State machine
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE;
            prefetch_idx <= 0;
            req_id <= 0;
            req_layer <= 0;
            req_pos <= 0;
            req_depth_k <= 0;
        end else begin
            state <= next_state;
            
            case (state)
                IDLE: begin
                    if (prefetch_req_valid && prefetch_req_ready) begin
                        req_id <= prefetch_req_id;
                        req_layer <= prefetch_req_layer;
                        req_pos <= prefetch_req_pos;
                        req_depth_k <= prefetch_req_depth_k;
                        req_tokens <= prefetch_req_tokens;
                        prefetch_idx <= 0;
                    end
                end
                
                WAIT_LSTM: begin
                    if (lstm_resp_valid && lstm_resp_ready) begin
                        pred_tokens <= lstm_resp_tokens;
                        // Unpack confidences (simplified)
                        for (int i = 0; i < DEPTH_K; i++) begin
                            pred_confidences[i] <= lstm_resp_confidences[i*8 +: 8];
                        end
                    end
                end
                
                TRANSLATE_ADDR: begin
                    if (atu_resp_valid && atu_resp_ready) begin
                        phys_addr <= atu_resp_paddr;
                    end
                end
                
                ISSUE_DMA: begin
                    if (dma_desc_valid && dma_desc_ready) begin
                        prefetch_idx <= prefetch_idx + 1;
                    end
                end
            endcase
        end
    end
    
    // State machine logic
    always_comb begin
        next_state = state;
        
        prefetch_req_ready = 1'b0;
        lstm_req_valid = 1'b0;
        lstm_req_tokens = req_tokens;
        lstm_resp_ready = 1'b0;
        atu_req_valid = 1'b0;
        atu_req_vaddr = encode_virt_addr(req_id, req_layer, req_pos + prefetch_idx + 1);
        atu_resp_ready = 1'b0;
        dir_check_valid = 1'b0;
        dir_check_addr = phys_addr;
        dir_check_resp_ready = 1'b0;
        dma_desc_valid = 1'b0;
        dma_desc_data = 0;
        
        case (state)
            IDLE: begin
                if (prefetch_req_valid) begin
                    prefetch_req_ready = 1'b1;
                    next_state = REQUEST_LSTM;
                end
            end
            
            REQUEST_LSTM: begin
                lstm_req_valid = 1'b1;
                if (lstm_req_ready) begin
                    next_state = WAIT_LSTM;
                end
            end
            
            WAIT_LSTM: begin
                lstm_resp_ready = 1'b1;
                if (lstm_resp_valid) begin
                    next_state = TRANSLATE_ADDR;
                end
            end
            
            TRANSLATE_ADDR: begin
                atu_req_valid = 1'b1;
                if (atu_req_ready) begin
                    next_state = CHECK_DIR;
                end
            end
            
            CHECK_DIR: begin
                if (atu_resp_valid) begin
                    atu_resp_ready = 1'b1;
                    dir_check_valid = 1'b1;
                    if (dir_check_ready) begin
                        next_state = ISSUE_DMA;
                    end
                end
            end
            
            ISSUE_DMA: begin
                if (dir_check_resp_valid) begin
                    dir_check_resp_ready = 1'b1;
                    
                    // Only issue DMA if not in L1/L2
                    if (!dir_check_hit) begin
                        // Build DMA descriptor
                        dma_desc_data[63:0] = phys_addr;   // fpga_addr
                        dma_desc_data[127:64] = 64'h8000000000 + (phys_addr & 64'hFFFFFFFFFFFF);  // gpu_addr (L2 buffer)
                        dma_desc_data[95:64] = 32'd4096;   // bytes (page size)
                        dma_desc_data[103:96] = 8'b00000100;  // flags: PREFETCH bit set
                        
                        dma_desc_valid = 1'b1;
                        if (dma_desc_ready) begin
                            if (prefetch_idx < req_depth_k - 1) begin
                                next_state = TRANSLATE_ADDR;  // Next token
                            end else begin
                                next_state = DONE;
                            end
                        end
                    end else begin
                        // Already cached, skip
                        if (prefetch_idx < req_depth_k - 1) begin
                            prefetch_idx = prefetch_idx + 1;
                            next_state = TRANSLATE_ADDR;
                        end else begin
                            next_state = DONE;
                        end
                    end
                end
            end
            
            DONE: begin
                next_state = IDLE;
            end
        endcase
    end

endmodule

