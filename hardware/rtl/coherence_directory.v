// hardware/rtl/coherence_directory.v
// Directory-based coherence controller for CXL-SpecKV
// This module acts as the CXL home agent and maintains coherence state
// for all KV-cache entries across GPU L1, L2 prefetch buffer, and CXL memory

module coherence_directory #(
    parameter ADDR_WIDTH = 64,
    parameter DATA_WIDTH = 512,
    parameter NUM_ENTRIES = 4096,        // Directory entries
    parameter CACHE_LINE_SIZE = 64,      // CXL cache line size
    parameter NUM_SHARERS = 4            // Max number of sharers per line
)(
    input  wire                        clk,
    input  wire                        rst_n,
    
    // GPU request interface (from PCIe)
    input  wire                        gpu_req_valid,
    output wire                        gpu_req_ready,
    input  wire                        gpu_req_write,
    input  wire [ADDR_WIDTH-1:0]       gpu_req_addr,
    input  wire [DATA_WIDTH-1:0]       gpu_req_data,
    input  wire [DATA_WIDTH/8-1:0]     gpu_req_strb,
    
    output reg                         gpu_resp_valid,
    input  wire                        gpu_resp_ready,
    output reg  [DATA_WIDTH-1:0]       gpu_resp_data,
    output reg                         gpu_resp_error,
    
    // CXL.mem interface (to CXL memory pool)
    output reg                         cxl_mem_req_valid,
    input  wire                        cxl_mem_req_ready,
    output reg                         cxl_mem_req_write,
    output reg  [ADDR_WIDTH-1:0]       cxl_mem_req_addr,
    output reg  [DATA_WIDTH-1:0]       cxl_mem_req_data,
    
    input  wire                        cxl_mem_resp_valid,
    output reg                         cxl_mem_resp_ready,
    input  wire [DATA_WIDTH-1:0]       cxl_mem_resp_data,
    
    // CXL.cache coherence interface (invalidations, writebacks)
    output reg                         cxl_cache_inv_valid,
    input  wire                        cxl_cache_inv_ready,
    output reg  [ADDR_WIDTH-1:0]       cxl_cache_inv_addr,
    output reg  [1:0]                  cxl_cache_inv_type,  // 0:inval, 1:wb, 2:flush
    
    input  wire                        cxl_cache_snoop_valid,
    output reg                         cxl_cache_snoop_ready,
    input  wire [ADDR_WIDTH-1:0]       cxl_cache_snoop_addr,
    
    // Status and statistics
    output wire [31:0]                 dir_num_entries_used,
    output wire [31:0]                 dir_num_shared,
    output wire [31:0]                 dir_num_exclusive,
    output wire [31:0]                 dir_num_modified,
    output wire [31:0]                 coherence_ops_count
);

    // Directory state encoding (MESI-like)
    localparam [1:0] STATE_INVALID   = 2'b00;
    localparam [1:0] STATE_SHARED    = 2'b01;
    localparam [1:0] STATE_EXCLUSIVE = 2'b10;
    localparam [1:0] STATE_MODIFIED  = 2'b11;
    
    // Coherence operation types
    localparam [1:0] OP_INVALIDATE  = 2'b00;
    localparam [1:0] OP_WRITEBACK   = 2'b01;
    localparam [1:0] OP_FLUSH       = 2'b10;
    
    // Directory entry structure
    // [1:0] state, [NUM_SHARERS-1:0] sharer_bitmap, [ADDR_WIDTH-1:0] tag
    reg [1:0] dir_state [0:NUM_ENTRIES-1];
    reg [NUM_SHARERS-1:0] dir_sharers [0:NUM_ENTRIES-1];
    reg [ADDR_WIDTH-1:0] dir_tag [0:NUM_ENTRIES-1];
    reg dir_valid [0:NUM_ENTRIES-1];
    
    // Pending operation tracking
    reg [ADDR_WIDTH-1:0] pending_addr;
    reg [DATA_WIDTH-1:0] pending_data;
    reg pending_write;
    reg pending_gpu_req;
    
    // FSM states
    localparam [2:0] FSM_IDLE           = 3'b000;
    localparam [2:0] FSM_DIR_LOOKUP     = 3'b001;
    localparam [2:0] FSM_SEND_INVAL     = 3'b010;
    localparam [2:0] FSM_WAIT_INVAL_ACK = 3'b011;
    localparam [2:0] FSM_CXL_READ       = 3'b100;
    localparam [2:0] FSM_CXL_WRITE      = 3'b101;
    localparam [2:0] FSM_RESPOND        = 3'b110;
    
    reg [2:0] state;
    reg [2:0] next_state;
    
    // Directory index calculation (hash of address)
    wire [$clog2(NUM_ENTRIES)-1:0] dir_idx = gpu_req_addr[$clog2(NUM_ENTRIES)+$clog2(CACHE_LINE_SIZE)-1:$clog2(CACHE_LINE_SIZE)] % NUM_ENTRIES;
    wire [$clog2(NUM_ENTRIES)-1:0] pending_dir_idx = pending_addr[$clog2(NUM_ENTRIES)+$clog2(CACHE_LINE_SIZE)-1:$clog2(CACHE_LINE_SIZE)] % NUM_ENTRIES;
    
    // Directory lookup results
    reg dir_hit;
    reg [1:0] dir_state_out;
    reg [NUM_SHARERS-1:0] dir_sharers_out;
    
    // Statistics counters
    reg [31:0] entries_used_count;
    reg [31:0] shared_count;
    reg [31:0] exclusive_count;
    reg [31:0] modified_count;
    reg [31:0] coherence_ops;
    
    assign gpu_req_ready = (state == FSM_IDLE) && !pending_gpu_req;
    
    // FSM state transition
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= FSM_IDLE;
        end else begin
            state <= next_state;
        end
    end
    
    // FSM next state logic
    always_comb begin
        next_state = state;
        case (state)
            FSM_IDLE: begin
                if (gpu_req_valid && gpu_req_ready) begin
                    next_state = FSM_DIR_LOOKUP;
                end
            end
            
            FSM_DIR_LOOKUP: begin
                if (dir_hit) begin
                    // Hit in directory
                    if (pending_write) begin
                        // Write request - need to invalidate sharers
                        if (dir_state_out == STATE_SHARED || 
                            dir_state_out == STATE_EXCLUSIVE) begin
                            next_state = FSM_SEND_INVAL;
                        end else begin
                            next_state = FSM_CXL_WRITE;
                        end
                    end else begin
                        // Read request
                        next_state = FSM_CXL_READ;
                    end
                end else begin
                    // Miss - allocate new entry
                    if (pending_write) begin
                        next_state = FSM_CXL_WRITE;
                    end else begin
                        next_state = FSM_CXL_READ;
                    end
                end
            end
            
            FSM_SEND_INVAL: begin
                if (cxl_cache_inv_valid && cxl_cache_inv_ready) begin
                    next_state = FSM_WAIT_INVAL_ACK;
                end
            end
            
            FSM_WAIT_INVAL_ACK: begin
                // In real implementation, wait for ack from all sharers
                // For simplicity, assume immediate ack
                if (pending_write) begin
                    next_state = FSM_CXL_WRITE;
                end else begin
                    next_state = FSM_CXL_READ;
                end
            end
            
            FSM_CXL_READ: begin
                if (cxl_mem_resp_valid && cxl_mem_resp_ready) begin
                    next_state = FSM_RESPOND;
                end
            end
            
            FSM_CXL_WRITE: begin
                if (cxl_mem_req_valid && cxl_mem_req_ready) begin
                    next_state = FSM_RESPOND;
                end
            end
            
            FSM_RESPOND: begin
                if (gpu_resp_valid && gpu_resp_ready) begin
                    next_state = FSM_IDLE;
                end
            end
            
            default: next_state = FSM_IDLE;
        endcase
    end
    
    // FSM output logic
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            pending_gpu_req <= 1'b0;
            pending_addr <= 64'd0;
            pending_data <= {DATA_WIDTH{1'b0}};
            pending_write <= 1'b0;
            
            cxl_mem_req_valid <= 1'b0;
            cxl_mem_resp_ready <= 1'b0;
            cxl_cache_inv_valid <= 1'b0;
            cxl_cache_snoop_ready <= 1'b0;
            gpu_resp_valid <= 1'b0;
            
            dir_hit <= 1'b0;
            dir_state_out <= STATE_INVALID;
            dir_sharers_out <= {NUM_SHARERS{1'b0}};
            
            entries_used_count <= 32'd0;
            coherence_ops <= 32'd0;
            
        end else begin
            case (state)
                FSM_IDLE: begin
                    // Accept new GPU request
                    if (gpu_req_valid && gpu_req_ready) begin
                        pending_gpu_req <= 1'b1;
                        pending_addr <= gpu_req_addr;
                        pending_data <= gpu_req_data;
                        pending_write <= gpu_req_write;
                    end
                    gpu_resp_valid <= 1'b0;
                    cxl_mem_req_valid <= 1'b0;
                    cxl_cache_inv_valid <= 1'b0;
                end
                
                FSM_DIR_LOOKUP: begin
                    // Lookup directory
                    if (dir_valid[pending_dir_idx] && 
                        dir_tag[pending_dir_idx] == pending_addr[ADDR_WIDTH-1:$clog2(CACHE_LINE_SIZE)]) begin
                        dir_hit <= 1'b1;
                        dir_state_out <= dir_state[pending_dir_idx];
                        dir_sharers_out <= dir_sharers[pending_dir_idx];
                    end else begin
                        dir_hit <= 1'b0;
                        dir_state_out <= STATE_INVALID;
                    end
                end
                
                FSM_SEND_INVAL: begin
                    // Send invalidation to sharers
                    cxl_cache_inv_valid <= 1'b1;
                    cxl_cache_inv_addr <= pending_addr;
                    cxl_cache_inv_type <= OP_INVALIDATE;
                    
                    if (cxl_cache_inv_valid && cxl_cache_inv_ready) begin
                        coherence_ops <= coherence_ops + 1;
                        // Update directory state to INVALID
                        dir_state[pending_dir_idx] <= STATE_INVALID;
                        dir_sharers[pending_dir_idx] <= {NUM_SHARERS{1'b0}};
                    end
                end
                
                FSM_WAIT_INVAL_ACK: begin
                    cxl_cache_inv_valid <= 1'b0;
                    // Wait for acknowledgments (simplified)
                end
                
                FSM_CXL_READ: begin
                    // Issue CXL memory read
                    if (!cxl_mem_req_valid) begin
                        cxl_mem_req_valid <= 1'b1;
                        cxl_mem_req_write <= 1'b0;
                        cxl_mem_req_addr <= pending_addr;
                        cxl_mem_resp_ready <= 1'b1;
                    end
                    
                    if (cxl_mem_req_valid && cxl_mem_req_ready) begin
                        cxl_mem_req_valid <= 1'b0;
                    end
                    
                    if (cxl_mem_resp_valid && cxl_mem_resp_ready) begin
                        // Update directory to SHARED state
                        dir_valid[pending_dir_idx] <= 1'b1;
                        dir_tag[pending_dir_idx] <= pending_addr[ADDR_WIDTH-1:$clog2(CACHE_LINE_SIZE)];
                        dir_state[pending_dir_idx] <= STATE_SHARED;
                        dir_sharers[pending_dir_idx] <= 4'b0001; // GPU is sharer 0
                        
                        gpu_resp_data <= cxl_mem_resp_data;
                        cxl_mem_resp_ready <= 1'b0;
                    end
                end
                
                FSM_CXL_WRITE: begin
                    // Issue CXL memory write
                    if (!cxl_mem_req_valid) begin
                        cxl_mem_req_valid <= 1'b1;
                        cxl_mem_req_write <= 1'b1;
                        cxl_mem_req_addr <= pending_addr;
                        cxl_mem_req_data <= pending_data;
                    end
                    
                    if (cxl_mem_req_valid && cxl_mem_req_ready) begin
                        cxl_mem_req_valid <= 1'b0;
                        
                        // Update directory to MODIFIED state
                        dir_valid[pending_dir_idx] <= 1'b1;
                        dir_tag[pending_dir_idx] <= pending_addr[ADDR_WIDTH-1:$clog2(CACHE_LINE_SIZE)];
                        dir_state[pending_dir_idx] <= STATE_MODIFIED;
                        dir_sharers[pending_dir_idx] <= 4'b0001; // GPU owns the line
                    end
                end
                
                FSM_RESPOND: begin
                    // Send response to GPU
                    gpu_resp_valid <= 1'b1;
                    gpu_resp_error <= 1'b0;
                    
                    if (gpu_resp_valid && gpu_resp_ready) begin
                        gpu_resp_valid <= 1'b0;
                        pending_gpu_req <= 1'b0;
                    end
                end
            endcase
            
            // Handle snoop requests from CXL (other agents)
            if (cxl_cache_snoop_valid && !cxl_cache_snoop_ready) begin
                cxl_cache_snoop_ready <= 1'b1;
                // Lookup and respond (simplified)
            end else begin
                cxl_cache_snoop_ready <= 1'b0;
            end
        end
    end
    
    // Statistics counting
    integer i;
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            entries_used_count <= 32'd0;
            shared_count <= 32'd0;
            exclusive_count <= 32'd0;
            modified_count <= 32'd0;
        end else begin
            // Recount periodically (simplified - in practice use incremental updates)
            entries_used_count = 32'd0;
            shared_count = 32'd0;
            exclusive_count = 32'd0;
            modified_count = 32'd0;
            
            for (i = 0; i < NUM_ENTRIES; i = i + 1) begin
                if (dir_valid[i]) begin
                    entries_used_count = entries_used_count + 1;
                    case (dir_state[i])
                        STATE_SHARED: shared_count = shared_count + 1;
                        STATE_EXCLUSIVE: exclusive_count = exclusive_count + 1;
                        STATE_MODIFIED: modified_count = modified_count + 1;
                    endcase
                end
            end
        end
    end
    
    assign dir_num_entries_used = entries_used_count;
    assign dir_num_shared = shared_count;
    assign dir_num_exclusive = exclusive_count;
    assign dir_num_modified = modified_count;
    assign coherence_ops_count = coherence_ops;

endmodule
