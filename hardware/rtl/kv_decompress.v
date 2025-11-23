// hardware/rtl/kv_decompress.v
// KV-Cache Decompression Pipeline (inverse of compression)
// RLE decode -> Delta decode -> Dequantize (INT8 -> FP16)

module kv_decompress #(
    parameter N = 1024,
    parameter D = 128,
    parameter IN_W = 8,        // INT8 input
    parameter OUT_W = 16,      // FP16 output
    parameter SCALE_W = 16
)(
    input  wire                        clk,
    input  wire                        rst_n,
    
    // AXI-Stream input (compressed)
    input  wire                        s_axis_tvalid,
    output wire                        s_axis_tready,
    input  wire [IN_W*D-1:0]           s_axis_tdata,
    input  wire [SCALE_W-1:0]          s_axis_tscale,
    input  wire                        s_axis_tlast,
    
    // AXI-Stream output (FP16)
    output reg                         m_axis_tvalid,
    input  wire                        m_axis_tready,
    output reg  [OUT_W*D-1:0]          m_axis_tdata,
    output reg                         m_axis_tlast
);

    localparam PIPELINE_DEPTH = 20;
    
    // Stage 15-18 (inverse): RLE decode
    reg signed [IN_W-1:0] rle_decoded [D-1:0];
    reg signed [IN_W-1:0] rle_pipe [3:0][D-1:0];
    
    // Stage 9-14 (inverse): Delta decode
    reg signed [IN_W-1:0] delta_decoded [D-1:0];
    reg signed [IN_W-1:0] quantized [D-1:0];
    reg signed [IN_W-1:0] delta_pipe [5:0][D-1:0];
    reg signed [IN_W-1:0] quant_pipe [5:0][D-1:0];
    
    // Stage 5-8 (inverse): Dequantize
    reg [OUT_W-1:0] dequantized [D-1:0];
    reg [OUT_W-1:0] dequant_pipe [3:0][D-1:0];
    reg [SCALE_W-1:0] scale_pipe [3:0];
    
    // Stage 1-4: Output buffering
    reg [OUT_W*D-1:0] output_data;
    
    reg [PIPELINE_DEPTH-1:0] pipe_valid;
    
    assign s_axis_tready = !pipe_valid[PIPELINE_DEPTH-1] || m_axis_tready;
    
    // Stage 15-18: RLE decode (simplified - assumes input is already delta-encoded)
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            for (int i = 0; i < D; i++) begin
                rle_decoded[i] <= 0;
                for (int j = 0; j < 4; j++) begin
                    rle_pipe[j][i] <= 0;
                end
            end
        end else if (s_axis_tvalid && s_axis_tready) begin
            // Unpack input (simplified - real RLE decode would expand)
            for (int i = 0; i < D; i++) begin
                rle_pipe[0][i] <= $signed(s_axis_tdata[i*IN_W +: IN_W]);
                rle_pipe[1][i] <= rle_pipe[0][i];
                rle_pipe[2][i] <= rle_pipe[1][i];
                rle_pipe[3][i] <= rle_pipe[2][i];
                rle_decoded[i] <= rle_pipe[3][i];
            end
        end
    end
    
    // Stage 9-14: Delta decode (prefix sum)
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            for (int i = 0; i < D; i++) begin
                delta_decoded[i] <= 0;
                quantized[i] <= 0;
                for (int j = 0; j < 6; j++) begin
                    delta_pipe[j][i] <= 0;
                    quant_pipe[j][i] <= 0;
                end
            end
        end else begin
            // Delta decode: x_i = x_{i-1} + δ_i
            quantized[0] <= rle_decoded[0];
            for (int i = 1; i < D; i++) begin
                delta_pipe[0][i] <= rle_decoded[i];
                delta_pipe[1][i] <= delta_pipe[0][i];
                delta_pipe[2][i] <= delta_pipe[1][i];
                delta_pipe[3][i] <= delta_pipe[2][i];
                delta_pipe[4][i] <= delta_pipe[3][i];
                delta_pipe[5][i] <= delta_pipe[4][i];
                delta_decoded[i] <= delta_pipe[5][i];
                
                quant_pipe[0][i] <= quantized[i-1] + delta_decoded[i];
                quant_pipe[1][i] <= quant_pipe[0][i];
                quant_pipe[2][i] <= quant_pipe[1][i];
                quant_pipe[3][i] <= quant_pipe[2][i];
                quant_pipe[4][i] <= quant_pipe[3][i];
                quant_pipe[5][i] <= quant_pipe[4][i];
                quantized[i] <= quant_pipe[5][i];
            end
        end
    end
    
    // Stage 5-8: Dequantize (INT8 -> FP16)
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            for (int i = 0; i < D; i++) begin
                dequantized[i] <= 0;
                for (int j = 0; j < 4; j++) begin
                    dequant_pipe[j][i] <= 0;
                end
            end
            for (int j = 0; j < 4; j++) begin
                scale_pipe[j] <= 0;
            end
        end else begin
            // Dequantize: x_fp16 = (x_int8 / 127) * scale
            scale_pipe[0] <= s_axis_tscale;
            scale_pipe[1] <= scale_pipe[0];
            scale_pipe[2] <= scale_pipe[1];
            scale_pipe[3] <= scale_pipe[2];
            
            for (int i = 0; i < D; i++) begin
                // Simplified dequantization (real implementation needs FP16 multiplier)
                dequant_pipe[0][i] <= (quantized[i] * scale_pipe[3]) / 16'd127;
                dequant_pipe[1][i] <= dequant_pipe[0][i];
                dequant_pipe[2][i] <= dequant_pipe[1][i];
                dequant_pipe[3][i] <= dequant_pipe[2][i];
                dequantized[i] <= dequant_pipe[3][i];
            end
        end
    end
    
    // Stage 1-4: Output formatting
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            m_axis_tvalid <= 1'b0;
            m_axis_tdata <= 0;
            m_axis_tlast <= 1'b0;
        end else begin
            // Pack output
            for (int i = 0; i < D; i++) begin
                output_data[i*OUT_W +: OUT_W] <= dequantized[i];
            end
            
            m_axis_tvalid <= pipe_valid[PIPELINE_DEPTH-1];
            m_axis_tdata <= output_data;
            m_axis_tlast <= s_axis_tlast;
        end
    end
    
    // Pipeline valid propagation
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            pipe_valid <= 0;
        end else begin
            pipe_valid <= {pipe_valid[PIPELINE_DEPTH-2:0], s_axis_tvalid && s_axis_tready};
        end
    end

endmodule

