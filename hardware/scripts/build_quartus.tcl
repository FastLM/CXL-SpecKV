# hardware/scripts/build_quartus.tcl
# Quartus Prime build script for CXL-SpecKV

# Project settings
set project_name "cxl_speckv"
set top_module "cxl_speckv_top"
set device_family "Agilex"
set device_part "AGFB014R24A2E2V"

# Create project
project_new $project_name -overwrite

# Set device
set_global_assignment -name FAMILY $device_family
set_global_assignment -name DEVICE $device_part

# Add source files
set_global_assignment -name VERILOG_FILE ../rtl/atu.v
set_global_assignment -name VERILOG_FILE ../rtl/kv_compress.v
set_global_assignment -name VERILOG_FILE ../rtl/kv_decompress.v
set_global_assignment -name VERILOG_FILE ../rtl/dma_engine.v
set_global_assignment -name VERILOG_FILE ../rtl/cxl_mem_if.v
set_global_assignment -name VERILOG_FILE ../rtl/prefetch_core.v
set_global_assignment -name VERILOG_FILE ../rtl/cxl_speckv_top.v

# Set top-level module
set_global_assignment -name TOP_LEVEL_ENTITY $top_module

# Timing constraints
set_global_assignment -name FMAX_REQUIREMENT "800 MHz"

# Pin assignments (example - adjust for your board)
# set_location_assignment PIN_XX -to clk
# set_location_assignment PIN_YY -to rst_n

# Compilation settings
set_global_assignment -name OPTIMIZATION_MODE "HIGH PERFORMANCE EFFORT"
set_global_assignment -name SYNTH_TIMING_DRIVEN_SYNTHESIS ON
set_global_assignment -name TIMEQUEST_MULTICORNER_ANALYSIS ON

# Run compilation
load_package flow
execute_module -tool map
execute_module -tool fit
execute_module -tool sta
execute_module -tool asm

puts "Build complete!"

