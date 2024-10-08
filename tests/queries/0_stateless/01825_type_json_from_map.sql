-- Tags: no-fasttest, no-random-settings, no-random-merge-tree-settings
-- For example, it is 4 times slower with --merge_max_block_size=5967 --index_granularity=55 --min_bytes_for_wide_part=847510133

DROP TABLE IF EXISTS t_json;
DROP TABLE IF EXISTS t_map;

SET allow_experimental_object_type = 1;
SET optimize_trivial_insert_select = 1;

CREATE TABLE t_json(id UInt64, obj Object('json')) ENGINE = MergeTree ORDER BY id;
CREATE TABLE t_map(id UInt64, m Map(String, UInt64)) ENGINE = MergeTree ORDER BY id;

INSERT INTO t_map
SELECT
    number,
    (
        arrayMap(x -> 'col' || toString(x), range(number % 10)),
        range(number % 10)
    )::Map(String, UInt64)
FROM numbers(1000000);

INSERT INTO t_json SELECT id, m FROM t_map;
SELECT sum(m['col1']), sum(m['col4']), sum(m['col7']), sum(m['col8'] = 0) FROM t_map;
SELECT sum(obj.col1), sum(obj.col4), sum(obj.col7), sum(obj.col8 = 0) FROM t_json;
SELECT toTypeName(obj) FROM t_json LIMIT 1;

INSERT INTO t_json
SELECT
    number,
    (
        arrayMap(x -> 'col' || toString(x), range(number % 10)),
        range(number % 10)
    )::Map(FixedString(4), UInt64)
FROM numbers(1000000);

SELECT sum(obj.col1), sum(obj.col4), sum(obj.col7), sum(obj.col8 = 0) FROM t_json;

INSERT INTO t_json
SELECT number, (range(number % 10), range(number % 10))::Map(UInt64, UInt64)
FROM numbers(1000000); -- { serverError TYPE_MISMATCH }

DROP TABLE IF EXISTS t_json;
DROP TABLE IF EXISTS t_map;
