/*
** Copyright (c) 2022 LunarG, Inc.
** Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and associated documentation files (the "Software"),
** to deal in the Software without restriction, including without limitation
** the rights to use, copy, modify, merge, publish, distribute, sublicense,
** and/or sell copies of the Software, and to permit persons to whom the
** Software is furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
*/

#include "dx12_optimize_util.h"
#include "block_skipping_file_processor.h"
#include "dx12_resource_value_tracking_consumer.h"

#include "dx12_file_optimizer.h"
#include "decode/dx12_object_info.h"
#include "generated/generated_dx12_replay_consumer.h"
#include "decode/dx12_resource_value_tracker.h"
#include "decode/file_processor.h"

#include <map>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)

struct Dx12OptimizationInfo
{
    // PSO removal
    std::unordered_set<uint64_t>         unreferenced_blocks;
    decode::UnreferencedPsoCreationCalls calls_info{};

    // DXR optimization
    decode::Dx12FillCommandResourceValueMap  fill_command_resource_values;
    decode::Dx12UnassociatedResourceValueMap unassociated_resource_values;
};

void CreateResourceValueTrackingConsumer(
    decode::FileProcessor*                                      file_processor,
    std::unique_ptr<decode::Dx12ResourceValueTrackingConsumer>& dx12_replay_consumer,
    std::shared_ptr<application::Application>&                  application,
    const decode::Dx12OptimizationOptions&                      options)
{
    std::string app_string = "GFXReconstruct Optimizer - analyzing file";
    if (options.optimize_dxr_experimental)
    {
        app_string += ", image may appear corrupt";
    }

    // Dx12ReplayConsumer requires a windowed application.
    application = std::make_shared<gfxrecon::application::Application>(app_string, file_processor);
    application->InitializeDx12WsiContext();

    // Use default replay options, except dcp.
    decode::DxReplayOptions dx_replay_options;
    dx_replay_options.discard_cached_psos = true;

    // Create the replay consumer.
    dx12_replay_consumer = std::make_unique<decode::Dx12ResourceValueTrackingConsumer>(
        application, dx_replay_options, options.optimize_dxr_experimental);
    dx12_replay_consumer->SetFatalErrorHandler([](const char* message) { throw std::runtime_error(message); });

    if (options.optimize_dxr_experimental)
    {
        dx12_replay_consumer->EnableReplayOfResourceValueCalls(false);
    }
}

bool FileProcessorSucceeded(const decode::FileProcessor& processor)
{
    if ((processor.GetCurrentFrameNumber() > 0) == false)
    {
        GFXRECON_WRITE_CONSOLE("Did not detect any frames in the capture.");
    }

    if ((processor.GetErrorState() == gfxrecon::decode::FileProcessor::kErrorNone) == false)
    {
        GFXRECON_WRITE_CONSOLE("Encountered error while reading the capture.");
    }

    if ((processor.EntireFileWasProcessed()) == false)
    {
        GFXRECON_WRITE_CONSOLE("Did not reach the end of the capture.");
    }

    return (processor.GetCurrentFrameNumber() > 0) &&
           (processor.GetErrorState() == gfxrecon::decode::FileProcessor::kErrorNone) &&
           processor.EntireFileWasProcessed();
}

bool BypassDxrOptimization(gfxrecon::decode::Dx12ObjectScanningConsumer& resref_consumer,
                           const decode::Dx12OptimizationOptions&        options)
{
    bool bypass_dxr_optimization = false;

    bool contains_dxr_workload = resref_consumer.ContainsDxrWorkload();
    bool is_dxr_optimized      = resref_consumer.ContainsOptFillMem();

    if ((contains_dxr_workload == false) || (is_dxr_optimized == true))
    {
        if (options.optimize_dxr == true)
        {
            if (contains_dxr_workload == false)
            {
                GFXRECON_WRITE_CONSOLE("Bypassing DXR optimization. Capture file does not contain DXR workloads.");
            }

            if (is_dxr_optimized == true)
            {
                GFXRECON_WRITE_CONSOLE(
                    "Bypassing DXR optimization. Capture file has already been optimized for DXR replay.");
            }
        }

        bypass_dxr_optimization = true;
    }

    return bypass_dxr_optimization;
}

bool GetPsoOptimizationInfo(const std::string&               input_filename,
                            decode::Dx12OptimizationOptions& options,
                            Dx12OptimizationInfo&            info)
{
    bool pso_scan_result = false;

    decode::FileProcessor pso_pass_file_processor;
    if (pso_pass_file_processor.Initialize(input_filename))
    {
        gfxrecon::decode::Dx12Decoder                pso_pass_decoder;
        gfxrecon::decode::Dx12ObjectScanningConsumer resref_consumer;

        GFXRECON_WRITE_CONSOLE("Scanning D3D12 capture %s for unreferenced PSOs.", input_filename.c_str());
        pso_pass_decoder.AddConsumer(&resref_consumer);
        pso_pass_file_processor.AddDecoder(&pso_pass_decoder);
        pso_pass_file_processor.ProcessAllFrames();
        if (FileProcessorSucceeded(pso_pass_file_processor))
        {
            resref_consumer.GetUnreferencedObjectCreationBlocks(&info.unreferenced_blocks, &info.calls_info);
            GFXRECON_WRITE_CONSOLE("Finished scanning capture file for unreferenced PSOs.");

            pso_scan_result = true;

            if (BypassDxrOptimization(resref_consumer, options) == true)
            {
                options.optimize_dxr = false;
            }
        }
        else if (pso_pass_file_processor.GetErrorState() != gfxrecon::decode::FileProcessor::kErrorNone)
        {
            GFXRECON_WRITE_CONSOLE("A failure has occurred during scanning capture file for unreferenced PSOs");
        }
        else if (!pso_pass_file_processor.EntireFileWasProcessed())
        {
            GFXRECON_WRITE_CONSOLE("Failed to process the entire capture file for unreferenced PSOs.");
        }
        else
        {
            GFXRECON_WRITE_CONSOLE("PSO removal optimization detected invalid capture. Please ensure that traces "
                                   "input to the optimizer "
                                   "already replay on their own.");
        }
    }

    return pso_scan_result;
}

bool GetDxrOptimizationInfo(const std::string&                     input_filename,
                            Dx12OptimizationInfo&                  info,
                            bool                                   first_pass,
                            const decode::Dx12OptimizationOptions& options)
{
    bool dxr_scan_result = false;

    std::shared_ptr<application::Application> application;
    decode::BlockSkippingFileProcessor        dxr_pass_file_processor;
    if (dxr_pass_file_processor.Initialize(input_filename))
    {
        decode::Dx12Decoder                                        dxr_pass_decoder;
        std::unique_ptr<decode::Dx12ResourceValueTrackingConsumer> resource_value_tracking_consumer = nullptr;

        CreateResourceValueTrackingConsumer(
            &dxr_pass_file_processor, resource_value_tracking_consumer, application, options);

        // If this is a second pass, set unassociated resource values on Dx12ResourceValueTracker.
        if (first_pass)
        {
            GFXRECON_WRITE_CONSOLE("Scanning D3D12 capture %s for DXR optimization information.",
                                   input_filename.c_str());
        }
        else
        {
            GFXRECON_WRITE_CONSOLE("Scanning D3D12 file %s another time for additional DXR optimization information.",
                                   input_filename.c_str());
            resource_value_tracking_consumer->SetUnassociatedResourceValues(
                std::move(info.fill_command_resource_values), std::move(info.unassociated_resource_values));
        }

        // Free up any existing info.
        info.fill_command_resource_values = decode::Dx12FillCommandResourceValueMap();
        info.unassociated_resource_values = decode::Dx12UnassociatedResourceValueMap();

        dxr_pass_decoder.AddConsumer(resource_value_tracking_consumer.get());

        dxr_pass_file_processor.AddDecoder(&dxr_pass_decoder);
        dxr_pass_file_processor.SetBlocksToSkip(info.unreferenced_blocks);

        GFXRECON_ASSERT(application != nullptr);

        application->Run();

        GFXRECON_ASSERT(dxr_pass_file_processor.IsSkippingFinished());

        if (FileProcessorSucceeded(dxr_pass_file_processor))
        {
            resource_value_tracking_consumer->GetTrackedResourceValues(info.fill_command_resource_values);
            resource_value_tracking_consumer->GetUnassociatedResourceValues(info.unassociated_resource_values);

            GFXRECON_WRITE_CONSOLE("Finished scanning capture file for DXR optimization.");

            dxr_scan_result = true;
        }
        else if (dxr_pass_file_processor.GetErrorState() != gfxrecon::decode::FileProcessor::kErrorNone)
        {
            GFXRECON_WRITE_CONSOLE("A failure has occurred during capture processing for DXR optimization");
        }
        else if (!dxr_pass_file_processor.EntireFileWasProcessed())
        {
            GFXRECON_WRITE_CONSOLE("Failed to process the entire capture file for DXR optimization.");
        }
        else
        {
            GFXRECON_WRITE_CONSOLE(
                "DXR optimization detected invalid capture. Please ensure that traces input to the optimizer "
                "already replay on their own.");
        }
    }

    return dxr_scan_result;
}

bool GetDx12OptimizationInfo(const std::string&               input_filename,
                             decode::Dx12OptimizationOptions& options,
                             Dx12OptimizationInfo&            info)
{
    bool pso_scan_result = true;
    bool dxr_scan_result = true;

    if (options.remove_redundant_psos)
    {
        pso_scan_result = GetPsoOptimizationInfo(input_filename, options, info);
    }

    if (options.optimize_dxr)
    {
        dxr_scan_result = GetDxrOptimizationInfo(input_filename, info, true, options);

        // If unassocaited resource values were found the resource value tracker must be run again.
        if (options.optimize_dxr_experimental && (info.unassociated_resource_values.size() > 0))
        {
            GFXRECON_WRITE_CONSOLE(
                "The first pass of experimental DXR optimization was unable to find all required optimization data. A "
                "second pass will attempt to find this data using a brute-force search.");
            dxr_scan_result = dxr_scan_result && GetDxrOptimizationInfo(input_filename, info, false, options);
        }
    }

    return pso_scan_result || dxr_scan_result;
}

bool ApplyDx12OptimizationInfo(const std::string&                     input_filename,
                               const std::string&                     output_filename,
                               const decode::Dx12OptimizationOptions& options,
                               const Dx12OptimizationInfo&            info)
{
    bool result                  = false;
    bool found_optimization_data = false;

    // Log info about PSO removal.
    if (options.remove_redundant_psos)
    {
        if (!info.unreferenced_blocks.empty())
        {
            found_optimization_data = true;

            const auto& calls_info = info.calls_info;

            GFXRECON_WRITE_CONSOLE("Removing %" PRIu64 " unused PSO related calls.", info.unreferenced_blocks.size());
            if (calls_info.graphics_pso_creation_calls > 0)
            {
                GFXRECON_WRITE_CONSOLE("Removing %" PRIu64 " CreateGraphicsPipelineState() calls.",
                                       calls_info.graphics_pso_creation_calls);
            }
            if (calls_info.compute_pso_creation_calls > 0)
            {
                GFXRECON_WRITE_CONSOLE("Removing %" PRIu64 " CreateComputePipelineState() calls.",
                                       calls_info.compute_pso_creation_calls);
            }
            if (calls_info.storepipeline_calls > 0)
            {
                GFXRECON_WRITE_CONSOLE("Removing %" PRIu64 " StorePipeline() calls.", calls_info.storepipeline_calls);
            }
        }
        else
        {
            GFXRECON_WRITE_CONSOLE("No redundant PSOs detected. Skipping PSO removal optimization.",
                                   input_filename.c_str());
        }
    }

    // Log info about DXR optimization
    if (options.optimize_dxr)
    {
        if (!info.fill_command_resource_values.empty())
        {
            found_optimization_data = true;
            GFXRECON_WRITE_CONSOLE("Optimizing %zu FillMemoryCommand blocks for DXR replay.",
                                   info.fill_command_resource_values.size());
        }
        else
        {
            GFXRECON_WRITE_CONSOLE("Found no DXR optimization info. Skipping DXR optimization.");
        }
    }

    // Verify that some optimization info was found.
    if (!found_optimization_data)
    {
        GFXRECON_WRITE_CONSOLE("No optimizable data was found. A new capture file will not be created.");
        result = true;
    }
    else
    {
        // Write optimized capture file.
        GFXRECON_WRITE_CONSOLE("Writing optimized file.");

        gfxrecon::Dx12FileOptimizer file_optimizer;
        if (file_optimizer.Initialize(input_filename, output_filename))
        {
            file_optimizer.SetUnreferencedBlocks(info.unreferenced_blocks);
            file_optimizer.SetFillCommandResourceValues(&info.fill_command_resource_values);

            file_optimizer.Process();

            if (file_optimizer.GetErrorState() != gfxrecon::FileOptimizer::kErrorNone)
            {
                GFXRECON_WRITE_CONSOLE("A failure has occurred during capture processing (error=%d). If it was "
                                       "created, the output file may be invalid.",
                                       file_optimizer.GetErrorState());
            }
            else
            {
                // In a way, "resultant_objects = 0" will prove the two scan passes match.
                uint64_t resultant_objects = file_optimizer.GetUnreferencedBlocksSize();

                // Check that all fill commands were optimized.
                uint64_t optimized_fill_commands = file_optimizer.GetNumOptimizedFillCommands();
                uint64_t expected_fill_commands  = info.fill_command_resource_values.size();

                if (resultant_objects > 0)
                {
                    GFXRECON_WRITE_CONSOLE("Failed to remove all blocks for PSO removal optimization (%" PRIu64
                                           " unoptimized blocks). The output file may be invalid.",
                                           resultant_objects);
                }

                if (optimized_fill_commands != expected_fill_commands)
                {
                    GFXRECON_WRITE_CONSOLE("Failed to optimize all blocks for DXR optimization (%" PRIu64
                                           " unoptimized blocks). The output file may be invalid.",
                                           (expected_fill_commands - optimized_fill_commands));
                }

                result = true;
            }

            GFXRECON_WRITE_CONSOLE("Optimization complete.");
            GFXRECON_WRITE_CONSOLE("\tOriginal file size:  %" PRIu64 " bytes", file_optimizer.GetNumBytesRead());
            GFXRECON_WRITE_CONSOLE("\tOptimized file size: %" PRIu64 " bytes", file_optimizer.GetNumBytesWritten());
        }
    }

    return result;
}

bool Dx12OptimizeFile(std::string input_filename, std::string output_filename, decode::Dx12OptimizationOptions& options)
{
    // Return early if no DX12 optimizations were enabled.
    if (!options.remove_redundant_psos && !options.optimize_dxr)
    {
        return true;
    }

    // Run a scanning pass to collect the necessary optimization info.
    Dx12OptimizationInfo info;
    bool                 scan_result = GetDx12OptimizationInfo(input_filename, options, info);
    if (scan_result == false)
    {
        GFXRECON_WRITE_CONSOLE("File processing has encountered a fatal error and cannot continue.");
        return false;
    }

    // Use the result of the scanning pass to write an optimized file.
    bool filter_result = ApplyDx12OptimizationInfo(input_filename, output_filename, options, info);
    if (filter_result == false)
    {
        GFXRECON_WRITE_CONSOLE("Failure creating the optimized file.");
        return false;
    }

    return true;
}

GFXRECON_END_NAMESPACE(gfxrecon)
