// hardware/rtl/kv_compress.v
// KV-Cache Compression Pipeline (Algorithm 2 from paper)
// 20-stage pipeline: Scaling -> Quantization -> Delta -> RLE

module kv_compress #(
    parameter N = 1024,        // Number of tokens
    parameter D = 128,        // Hidden dimension per head
    parameter IN_W = 16,       // FP16 input width
    parameter OUT_W = 8,       // INT8 output width
    parameter SCALE_W = 16     // Scale factor width
)(
    input  wire                        clk,
    input  wire                        rst_n,
    
    // AXI-Stream input (FP16)
    input  wire                        s_axis_tvalid,
    output wire                        s_axis_tready,
    input  wire [IN_W*D-1:0]           s_axis_tdata,  // One row per cycle
    input  wire                        s_axis_tlast,
    
    // AXI-Stream output (compressed)
    output reg                         m_axis_tvalid,
    input  wire                        m_axis_tready,
    output reg  [OUT_W*D-1:0]          m_axis_tdata,
    output reg  [SCALE_W-1:0]           m_axis_tscale,  // Scale factor
    output reg                         m_axis_tlast
);

    // Pipeline stages
    // Stage 1-4: Input buffering and max(|x|) computation
    // Stage 5-8: Scale computation s = max(|x|) / 127
    // Stage 9-14: Quantization (FP16 -> INT8) + Delta encoding
    // Stage 15-18: Run-length encoding
    // Stage 19-20: Output formatting
    
    localparam PIPELINE_DEPTH = 20;
    
    // Stage 1-4: Find maximum absolute value
    reg [IN_W-1:0] stage1_data [D-1:0];
    reg [IN_W-1:0] stage2_data [D-1:0];
    reg [IN_W-1:0] stage3_data [D-1:0];
    reg [IN_W-1:0] stage4_data [D-1:0];
    reg [IN_W-1:0] max_abs [D-1:0];
    
    // Stage 5-8: Compute scale factor
    reg [SCALE_W-1:0] scale_factor;
    reg [SCALE_W-1:0] scale_pipe [3:0];
    
    // Stage 9-14: Quantization and delta encoding
    reg signed [OUT_W-1:0] quantized [D-1:0];
    reg signed [OUT_W-1:0] delta [D-1:0];
    reg signed [OUT_W-1:0] quant_pipe [5:0][D-1:0];
    reg signed [OUT_W-1:0] delta_pipe [5:0][D-1:0];
    
    // Stage 15-18: RLE encoding
    reg [OUT_W-1:0] rle_data [D*2-1:0];  // [value, count] pairs
    reg [7:0] rle_count;
    reg [OUT_W-1:0] rle_value;
    
    // Stage 19-20: Output
    reg [OUT_W*D-1:0] output_data;
    reg [SCALE_W-1:0] output_scale;
    
    // Pipeline valid signals
    reg [PIPELINE_DEPTH-1:0] pipe_valid;
    
    // Input ready
    assign s_axis_tready = !pipe_valid[PIPELINE_DEPTH-1] || m_axis_tready;
    
    // Stage 1-4: Find max absolute value per element
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            for (int i = 0; i < D; i++) begin
                stage1_data[i] <= 0;
                stage2_data[i] <= 0;
                stage3_data[i] <= 0;
                stage4_data[i] <= 0;
                max_abs[i] <= 0;
            end
        end else if (s_axis_tvalid && s_axis_tready) begin
            // Unpack input
            for (int i = 0; i < D; i++) begin
                stage1_data[i] <= s_axis_tdata[i*IN_W +: IN_W];
            end
            
            // Pipeline stages
            stage2_data <= stage1_data;
            stage3_data <= stage2_data;
            stage4_data <= stage3_data;
            
            // Find max (simplified - in real implementation would use tree reduction)
            for (int i = 0; i < D; i++) begin
                if (stage4_data[i] > max_abs[i]) begin
                    max_abs[i] <= stage4_data[i];
                end
            end
        end
    end
    
    // Stage 5-8: Compute scale factor
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            scale_factor <= 16'h7F00;  // 127.0 in FP16
            for (int i = 0; i < 4; i++) begin
                scale_pipe[i] <= 0;
            end
        end else begin
            // Scale = max / 127 (simplified division)
            // In real implementation: use FP16 divider
            scale_pipe[0] <= max_abs[0];  // Use first element's max
            scale_pipe[1] <= scale_pipe[0];
            scale_pipe[2] <= scale_pipe[1];
            scale_pipe[3] <= scale_pipe[2];
            scale_factor <= scale_pipe[3] / 16'd127;
        end
    end
    
    // Stage 9-14: Quantization and delta encoding
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            for (int i = 0; i < D; i++) begin
                quantized[i] <= 0;
                delta[i] <= 0;
                for (int j = 0; j < 6; j++) begin
                    quant_pipe[j][i] <= 0;
                    delta_pipe[j][i] <= 0;
                end
            end
        end else begin
            // Quantize: x_int8 = round(x_fp16 / scale * 127)
            for (int i = 0; i < D; i++) begin
                // Simplified quantization (real implementation needs FP16 arithmetic)
                quant_pipe[0][i] <= $signed(stage4_data[i] / scale_factor * 8'd127);
                quant_pipe[1][i] <= quant_pipe[0][i];
                quant_pipe[2][i] <= quant_pipe[1][i];
                quant_pipe[3][i] <= quant_pipe[2][i];
                quant_pipe[4][i] <= quant_pipe[3][i];
                quant_pipe[5][i] <= quant_pipe[4][i];
                quantized[i] <= quant_pipe[5][i];
            end
            
            // Delta encoding: δ_i = x_i - x_{i-1}
            delta[0] <= quantized[0];
            for (int i = 1; i < D; i++) begin
                delta_pipe[0][i] <= quantized[i] - quantized[i-1];
                delta_pipe[1][i] <= delta_pipe[0][i];
                delta_pipe[2][i] <= delta_pipe[1][i];
                delta_pipe[3][i] <= delta_pipe[2][i];
                delta_pipe[4][i] <= delta_pipe[3][i];
                delta_pipe[5][i] <= delta_pipe[4][i];
                delta[i] <= delta_pipe[5][i];
            end
        end
    end
    
    // Stage 15-18: Run-length encoding
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            rle_count <= 0;
            rle_value <= 0;
            for (int i = 0; i < D*2; i++) begin
                rle_data[i] <= 0;
            end
        end else begin
            // Simplified RLE: encode [value, count] pairs
            // Real implementation would pack more efficiently
            rle_data[0] <= delta[0];
            rle_data[1] <= 8'd1;
            // ... (simplified - full RLE would need state machine)
        end
    end
    
    // Stage 19-20: Output formatting
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            m_axis_tvalid <= 1'b0;
            m_axis_tdata <= 0;
            m_axis_tscale <= 0;
            m_axis_tlast <= 1'b0;
        end else begin
            // Pack output
            for (int i = 0; i < D; i++) begin
                output_data[i*OUT_W +: OUT_W] <= delta[i];
            end
            output_scale <= scale_factor;
            
            m_axis_tvalid <= pipe_valid[PIPELINE_DEPTH-1];
            m_axis_tdata <= output_data;
            m_axis_tscale <= output_scale;
            m_axis_tlast <= s_axis_tlast;  // Pass through
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

