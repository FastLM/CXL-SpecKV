#include "lstm_predictor.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <cstring>

namespace cxlspeckv {

LSTMPredictor::LSTMPredictor(
    size_t vocab_size,
    size_t embedding_dim,
    size_t hidden_dim,
    size_t num_layers,
    size_t history_length
) : vocab_size_(vocab_size),
    embedding_dim_(embedding_dim),
    hidden_dim_(hidden_dim),
    num_layers_(num_layers),
    history_length_(history_length)
{
    // Initialize model weights (simplified - in real implementation would load from file)
    embedding_weights_.resize(vocab_size_ * embedding_dim_, 0.0f);
    lstm_weights_.resize(num_layers_ * hidden_dim_ * hidden_dim_ * 4, 0.0f);  // 4 gates
    output_weights_.resize(hidden_dim_ * vocab_size_, 0.0f);
    
    // Initialize with small random values (Xavier initialization)
    for (size_t i = 0; i < embedding_weights_.size(); ++i) {
        embedding_weights_[i] = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.1f;
    }
    for (size_t i = 0; i < lstm_weights_.size(); ++i) {
        lstm_weights_[i] = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.1f;
    }
    for (size_t i = 0; i < output_weights_.size(); ++i) {
        output_weights_[i] = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.1f;
    }
}

LSTMPredictor::~LSTMPredictor() = default;

std::vector<std::pair<uint32_t, float>> LSTMPredictor::predict_top_k(
    const std::vector<uint32_t>& token_history,
    size_t k
) {
    // Ensure history length matches
    std::vector<uint32_t> history = token_history;
    if (history.size() > history_length_) {
        history = std::vector<uint32_t>(history.end() - history_length_, history.end());
    } else if (history.size() < history_length_) {
        // Pad with zeros
        history.insert(history.begin(), history_length_ - history.size(), 0);
    }
    
    // Initialize LSTM state
    LSTMState state;
    state.hidden.resize(hidden_dim_, 0.0f);
    state.cell.resize(hidden_dim_, 0.0f);
    
    // Process history through LSTM layers
    for (size_t t = 0; t < history_length_; ++t) {
        // Embed token
        std::vector<float> embedded = embed_token(history[t]);
        
        // Forward through LSTM layers
        for (size_t layer = 0; layer < num_layers_; ++layer) {
            size_t weight_offset = layer * hidden_dim_ * hidden_dim_ * 4;
            lstm_forward(embedded, state, 
                        std::vector<float>(lstm_weights_.begin() + weight_offset,
                                         lstm_weights_.begin() + weight_offset + hidden_dim_ * hidden_dim_ * 4));
        }
    }
    
    // Compute output probabilities
    std::vector<float> probs = compute_output_probs(state.hidden);
    
    // Get top-k tokens
    std::vector<std::pair<uint32_t, float>> token_probs;
    token_probs.reserve(vocab_size_);
    for (size_t i = 0; i < vocab_size_; ++i) {
        token_probs.emplace_back(static_cast<uint32_t>(i), probs[i]);
    }
    
    // Sort by probability (descending)
    std::sort(token_probs.begin(), token_probs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Return top-k
    std::vector<std::pair<uint32_t, float>> result;
    result.reserve(k);
    for (size_t i = 0; i < std::min(k, token_probs.size()); ++i) {
        result.push_back(token_probs[i]);
    }
    
    return result;
}

bool LSTMPredictor::load_model(const std::string& model_path) {
    // In real implementation, would load actual model weights from file
    // For now, return true (using initialized weights)
    return true;
}

bool LSTMPredictor::save_model(const std::string& model_path) const {
    // In real implementation, would save model weights to file
    return true;
}

size_t LSTMPredictor::get_model_size() const {
    // Model size in bytes (FP16 = 2 bytes per parameter)
    size_t embedding_params = vocab_size_ * embedding_dim_;
    size_t lstm_params = num_layers_ * hidden_dim_ * hidden_dim_ * 4;
    size_t output_params = hidden_dim_ * vocab_size_;
    size_t total_params = embedding_params + lstm_params + output_params;
    return total_params * 2;  // FP16 = 2 bytes
}

void LSTMPredictor::lstm_forward(
    const std::vector<float>& input,
    LSTMState& state,
    const std::vector<float>& weights
) {
    // Simplified LSTM forward pass
    // In real implementation, would compute all 4 gates (i, f, o, g)
    // For now, simplified update
    
    size_t input_dim = input.size();
    size_t hidden_dim = state.hidden.size();
    
    // Compute gates (simplified - would use proper matrix multiplication)
    for (size_t i = 0; i < hidden_dim; ++i) {
        // Input gate
        float i_gate = 0.5f;  // Simplified
        // Forget gate
        float f_gate = 0.5f;  // Simplified
        // Output gate
        float o_gate = 0.5f;  // Simplified
        // Candidate
        float g = 0.0f;
        for (size_t j = 0; j < input_dim && j < hidden_dim; ++j) {
            g += input[j] * 0.1f;  // Simplified weight
        }
        
        // Update cell state
        state.cell[i] = f_gate * state.cell[i] + i_gate * std::tanh(g);
        // Update hidden state
        state.hidden[i] = o_gate * std::tanh(state.cell[i]);
    }
}

std::vector<float> LSTMPredictor::embed_token(uint32_t token_id) {
    std::vector<float> embedding(embedding_dim_, 0.0f);
    
    if (token_id < vocab_size_) {
        size_t offset = token_id * embedding_dim_;
        for (size_t i = 0; i < embedding_dim_; ++i) {
            embedding[i] = embedding_weights_[offset + i];
        }
    }
    
    return embedding;
}

std::vector<float> LSTMPredictor::compute_output_probs(const std::vector<float>& hidden) {
    std::vector<float> logits(vocab_size_, 0.0f);
    
    // Compute logits (simplified - would use proper matrix multiplication)
    for (size_t i = 0; i < vocab_size_; ++i) {
        for (size_t j = 0; j < hidden_dim_ && j < hidden.size(); ++j) {
            size_t weight_idx = i * hidden_dim_ + j;
            if (weight_idx < output_weights_.size()) {
                logits[i] += hidden[j] * output_weights_[weight_idx];
            }
        }
    }
    
    // Softmax
    float max_logit = *std::max_element(logits.begin(), logits.end());
    float sum_exp = 0.0f;
    for (size_t i = 0; i < vocab_size_; ++i) {
        logits[i] = std::exp(logits[i] - max_logit);
        sum_exp += logits[i];
    }
    
    for (size_t i = 0; i < vocab_size_; ++i) {
        logits[i] /= sum_exp;
    }
    
    return logits;
}

} // namespace cxlspeckv

