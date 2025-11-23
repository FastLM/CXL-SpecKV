// hardware/rtl/atu.v
// Address Translation Unit (ATU) - Virtual to Physical Address Translation
// Implements TLB + page table walker as described in paper Section 3.5.1

module atu #(
    parameter VADDR_WIDTH = 64,
    parameter PADDR_WIDTH = 64,
    parameter TLB_ENTRIES = 256,
    parameter TLB_INDEX_WIDTH = 8,  // log2(TLB_ENTRIES)
    parameter PAGE_SIZE = 4096,
    parameter PAGE_OFFSET_WIDTH = 12  // log2(PAGE_SIZE)
)(
    input  wire                        clk,
    input  wire                        rst_n,
    
    // Translation request interface
    input  wire                        req_valid,
    input  wire [VADDR_WIDTH-1:0]      req_vaddr,
    output reg                         req_ready,
    
    output reg                         resp_valid,
    output reg  [PADDR_WIDTH-1:0]      resp_paddr,
    output reg                         resp_hit,  // 1=TLB hit, 0=page walk
    input  wire                        resp_ready,
    
    // Page table walker interface (to HBM/BRAM)
    output reg                        pt_rd_valid,
    output reg  [VADDR_WIDTH-1:0]     pt_rd_vaddr,
    input  wire                        pt_rd_ready,
    input  wire                        pt_rd_data_valid,
    input  wire [PADDR_WIDTH-1:0]      pt_rd_paddr
);

    // TLB entry structure
    typedef struct packed {
        logic [VADDR_WIDTH-PAGE_OFFSET_WIDTH-1:0] vpn;  // Virtual page number
        logic [PADDR_WIDTH-PAGE_OFFSET_WIDTH-1:0] pfn;  // Physical frame number
        logic valid;
        logic [7:0] asid;  // Address space ID (optional)
    } tlb_entry_t;
    
    // TLB storage (BRAM)
    tlb_entry_t [TLB_ENTRIES-1:0] tlb_array;
    reg [TLB_INDEX_WIDTH-1:0] tlb_wr_index;
    reg [TLB_INDEX_WIDTH-1:0] tlb_rd_index;
    reg tlb_wr_en;
    tlb_entry_t tlb_wr_data;
    tlb_entry_t tlb_rd_data;
    
    // Extract page number and offset
    wire [VADDR_WIDTH-PAGE_OFFSET_WIDTH-1:0] req_vpn;
    wire [PAGE_OFFSET_WIDTH-1:0] req_offset;
    assign req_vpn = req_vaddr[VADDR_WIDTH-1:PAGE_OFFSET_WIDTH];
    assign req_offset = req_vaddr[PAGE_OFFSET_WIDTH-1:0];
    
    // TLB index (simple hash)
    wire [TLB_INDEX_WIDTH-1:0] tlb_index;
    assign tlb_index = req_vpn[TLB_INDEX_WIDTH-1:0];
    
    // State machine
    typedef enum logic [1:0] {
        IDLE,
        TLB_LOOKUP,
        PAGE_WALK,
        RESPOND
    } state_t;
    
    state_t state, next_state;
    
    // TLB lookup
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE;
            for (int i = 0; i < TLB_ENTRIES; i++) begin
                tlb_array[i].valid <= 1'b0;
            end
        end else begin
            state <= next_state;
            
            // TLB write
            if (tlb_wr_en) begin
                tlb_array[tlb_wr_index] <= tlb_wr_data;
            end
            
            // TLB read
            tlb_rd_data <= tlb_array[tlb_rd_index];
        end
    end
    
    // State machine logic
    always_comb begin
        next_state = state;
        req_ready = 1'b0;
        resp_valid = 1'b0;
        resp_hit = 1'b0;
        pt_rd_valid = 1'b0;
        tlb_wr_en = 1'b0;
        tlb_rd_index = tlb_index;
        
        case (state)
            IDLE: begin
                if (req_valid) begin
                    next_state = TLB_LOOKUP;
                end
            end
            
            TLB_LOOKUP: begin
                // Check TLB hit
                if (tlb_rd_data.valid && (tlb_rd_data.vpn == req_vpn)) begin
                    // TLB hit
                    resp_valid = 1'b1;
                    resp_paddr = {tlb_rd_data.pfn, req_offset};
                    resp_hit = 1'b1;
                    if (resp_ready) begin
                        next_state = IDLE;
                    end else begin
                        next_state = RESPOND;
                    end
                end else begin
                    // TLB miss - start page walk
                    pt_rd_valid = 1'b1;
                    pt_rd_vaddr = {req_vpn, {PAGE_OFFSET_WIDTH{1'b0}}};
                    if (pt_rd_ready) begin
                        next_state = PAGE_WALK;
                    end
                end
            end
            
            PAGE_WALK: begin
                if (pt_rd_data_valid) begin
                    // Update TLB
                    tlb_wr_en = 1'b1;
                    tlb_wr_index = tlb_index;
                    tlb_wr_data.vpn = req_vpn;
                    tlb_wr_data.pfn = pt_rd_paddr[PADDR_WIDTH-1:PAGE_OFFSET_WIDTH];
                    tlb_wr_data.valid = 1'b1;
                    
                    // Respond
                    resp_valid = 1'b1;
                    resp_paddr = {pt_rd_paddr[PADDR_WIDTH-1:PAGE_OFFSET_WIDTH], req_offset};
                    resp_hit = 1'b0;
                    if (resp_ready) begin
                        next_state = IDLE;
                    end else begin
                        next_state = RESPOND;
                    end
                end
            end
            
            RESPOND: begin
                resp_valid = 1'b1;
                if (resp_ready) begin
                    next_state = IDLE;
                end
            end
        endcase
    end

endmodule

