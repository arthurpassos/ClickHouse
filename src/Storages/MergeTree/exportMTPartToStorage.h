#pragma once

#include <Storages/MergeTree/MergeTreeData.h>

namespace DB
{

void exportMTPartToStorage(const MergeTreeData & data, const MergeTreeData::DataPartPtr & data_part, SinkToStoragePtr dst_storage_sink, ContextPtr context);

void exportMTPartsToStorage(const MergeTreeData & data, const std::vector<MergeTreeData::DataPartPtr> & data_parts, std::shared_ptr<IStorage> dst_storage, ContextPtr context);

}
