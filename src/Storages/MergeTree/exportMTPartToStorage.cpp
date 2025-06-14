#include <Processors/Executors/CompletedPipelineExecutor.h>
#include <Processors/Executors/PullingPipelineExecutor.h>
#include <Processors/Formats/Impl/ParquetBlockOutputFormat.h>
#include <Processors/QueryPlan/BuildQueryPipelineSettings.h>
#include <Processors/QueryPlan/Optimizations/QueryPlanOptimizationSettings.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <Processors/Sinks/EmptySink.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Storages/MergeTree/MergeTreeSequentialSource.h>
#include <Storages/MergeTree/exportMTPartToStorage.h>

#include "Core/Settings.h"
#include "Interpreters/Context.h"
#include "Processors/QueryPlan/UnionStep.h"
#include "Storages/PartitionedSink.h"


namespace DB
{

namespace Setting
{
    extern const SettingsMaxThreads max_threads;
    extern const SettingsBool use_hive_partitioning;
}

void exportMTPartToStorage(const MergeTreeData & source_data, const MergeTreeData::DataPartPtr & data_part, SinkToStoragePtr dst_storage_sink, ContextPtr context)
{
    auto metadata_snapshot = source_data.getInMemoryMetadataPtr();
    Names columns_to_read = metadata_snapshot->getColumns().getNamesOfPhysical();
    StorageSnapshotPtr storage_snapshot = source_data.getStorageSnapshot(metadata_snapshot, context);

    MergeTreeData::IMutationsSnapshot::Params params
    {
        .metadata_version = metadata_snapshot->getMetadataVersion(),
        .min_part_metadata_version = data_part->getMetadataVersion(),
    };

    auto mutations_snapshot = source_data.getMutationsSnapshot(params);

    auto alter_conversions = MergeTreeData::getAlterConversionsForPart(
        data_part,
        mutations_snapshot,
        context);

    QueryPlan plan;

    // todoa arthur
    MergeTreeSequentialSourceType read_type = MergeTreeSequentialSourceType::Merge;

    bool apply_deleted_mask = true;
    bool read_with_direct_io = false;
    bool prefetch = false;

    createReadFromPartStep(
        read_type,
        plan,
        source_data,
        storage_snapshot,
        RangesInDataPart(data_part),
        alter_conversions,
        nullptr,
        columns_to_read,
        nullptr,
        apply_deleted_mask,
        std::nullopt,
        read_with_direct_io,
        prefetch,
        context,
        getLogger("abcde"));

    auto pipeline_settings = BuildQueryPipelineSettings(context);
    auto optimization_settings = QueryPlanOptimizationSettings(context);
    auto builder = plan.buildQueryPipeline(optimization_settings, pipeline_settings);

    QueryPipeline pipeline = QueryPipelineBuilder::getPipeline(std::move(*builder));

    pipeline.complete(std::move(dst_storage_sink));

    CompletedPipelineExecutor executor(pipeline);
    executor.execute();
}

void exportMTPartsToStorage(const MergeTreeData & source_data, const std::vector<MergeTreeData::DataPartPtr> & data_parts, std::shared_ptr<IStorage> dst_storage, ContextPtr context)
{
    std::vector<QueryPlanPtr> part_plans;
    part_plans.reserve(data_parts.size());

    auto metadata_snapshot = source_data.getInMemoryMetadataPtr();
    Names columns_to_read = metadata_snapshot->getColumns().getNamesOfPhysical();
    StorageSnapshotPtr storage_snapshot = source_data.getStorageSnapshot(metadata_snapshot, context);

    QueryPlan plan;

    // todoa arthur
    MergeTreeSequentialSourceType read_type = MergeTreeSequentialSourceType::Merge;

    bool apply_deleted_mask = true;
    bool read_with_direct_io = false;
    bool prefetch = false;

    QueryPipeline root_pipeline;

    for (const auto & data_part : data_parts)
    {
        MergeTreeData::IMutationsSnapshot::Params params
        {
            .metadata_version = metadata_snapshot->getMetadataVersion(),
            .min_part_metadata_version = data_part->getMetadataVersion(),
        };

        auto mutations_snapshot = source_data.getMutationsSnapshot(params);

        auto alter_conversions = MergeTreeData::getAlterConversionsForPart(
            data_part,
            mutations_snapshot,
            context);

        QueryPlan plan_for_part;

        createReadFromPartStep(
            read_type,
            plan_for_part,
            source_data,
            storage_snapshot,
            RangesInDataPart(data_part),
            alter_conversions,
            nullptr,
            columns_to_read,
            nullptr,
            apply_deleted_mask,
            std::nullopt,
            read_with_direct_io,
            prefetch,
            context,
            getLogger("ExportPartition"));

        QueryPlanOptimizationSettings optimization_settings(context);
        auto pipeline_settings = BuildQueryPipelineSettings(context);
        auto builder = plan_for_part.buildQueryPipeline(optimization_settings, pipeline_settings);
        auto pipeline = QueryPipelineBuilder::getPipeline(std::move(*builder));

        auto sink = dst_storage->importMergeTreePart(data_part->name, metadata_snapshot, context, dst_storage->areAsynchronousInsertsEnabled());
        pipeline.complete(sink);

        root_pipeline.addCompletedPipeline(std::move(pipeline));
    }

    root_pipeline.setNumThreads(context->getSettingsRef()[Setting::max_threads]);

    CompletedPipelineExecutor exec(root_pipeline);
    exec.execute();
}

}
