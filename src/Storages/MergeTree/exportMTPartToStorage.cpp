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

void exportMTPartsToStorage(const MergeTreeData & source_data, const std::vector<MergeTreeData::DataPartPtr> & data_parts, SinkToStoragePtr dst_storage_sink, ContextPtr context)
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
        auto plan_for_part = std::make_unique<QueryPlan>();

        createReadFromPartStep(
            read_type,
            *plan_for_part,
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

        part_plans.emplace_back(std::move(plan_for_part));
    }

    QueryPlan query_plan;
    if (part_plans.size() == 1)
        query_plan = std::move(*part_plans.front());
    else
    {
        Headers headers;
        headers.reserve(part_plans.size());
        for (auto & p : part_plans)
            headers.emplace_back(p->getCurrentHeader());

        auto union_step = std::make_unique<UnionStep>(std::move(headers));
        query_plan.unitePlans(std::move(union_step), std::move(part_plans));
    }

    QueryPlanOptimizationSettings optimization_settings(context);
    auto pipeline_settings = BuildQueryPipelineSettings(context);
    auto builder = query_plan.buildQueryPipeline(optimization_settings, pipeline_settings);
    auto pipeline = QueryPipelineBuilder::getPipeline(std::move(*builder));

    pipeline.setNumThreads(context->getSettingsRef()[Setting::max_threads]);

    dst_storage_sink->assumeSamePartition();

    pipeline.complete(dst_storage_sink);
    CompletedPipelineExecutor executor(pipeline);
    executor.execute();

    if (const auto * partitioned_sink = dynamic_cast<const PartitionedSink *>(dst_storage_sink.get()))
    {
        const auto stats = partitioned_sink->getPartitioningStats();
        std::cout<<"Finished export, stats:"<<std::endl<<"Partition id calculation: "<<stats.time_spent_on_partition_calculation<<std::endl<<"Chunk split: "<<stats.time_spent_on_chunk_split<<std::endl;
    }
}

}
