#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace cxlspeckv {

// Lightweight LSTM-based token predictor
// Architecture: 2-layer LSTM with 128 hidden units per layer
// Total parameters: ~128K (512KB in FP16)
class LSTMPredictor {
public:
    LSTMPredictor(
        size_t vocab_size = 32000,
        size_t embedding_dim = 64,
        size_t hidden_dim = 128,
        size_t num_layers = 2,
        size_t history_length = 16
    );
    
    ~LSTMPredictor();

    // Predict top-k tokens given history
    // Returns vector of (token_id, confidence) pairs
    std::vector<std::pair<uint32_t, float>> predict_top_k(
        const std::vector<uint32_t>& token_history,
        size_t k = 4
    );

    // Load pre-trained model weights
    bool load_model(const std::string& model_path);
    
    // Save model weights
    bool save_model(const std::string& model_path) const;
    
    // Get model size in bytes
    size_t get_model_size() const;

private:
    size_t vocab_size_;
    size_t embedding_dim_;
    size_t hidden_dim_;
    size_t num_layers_;
    size_t history_length_;
    
    // Model weights (simplified representation)
    // In real implementation, these would be actual weight tensors
    std::vector<float> embedding_weights_;
    std::vector<float> lstm_weights_;
    std::vector<float> output_weights_;
    
    // LSTM state
    struct LSTMState {
        std::vector<float> hidden;
        std::vector<float> cell;
    };
    
    // Forward pass through LSTM
    void lstm_forward(
        const std::vector<float>& input,
        LSTMState& state,
        const std::vector<float>& weights
    );
    
    // Embedding lookup
    std::vector<float> embed_token(uint32_t token_id);
    
    // Compute output probabilities
    std::vector<float> compute_output_probs(const std::vector<float>& hidden);
};

} // namespace cxlspeckv

