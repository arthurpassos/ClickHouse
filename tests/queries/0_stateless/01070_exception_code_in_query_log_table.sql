DROP TABLE IF EXISTS test_table_for_01070_exception_code_in_query_log_table;
SELECT * FROM test_table_for_01070_exception_code_in_query_log_table; -- { serverError UNKNOWN_TABLE }
CREATE TABLE test_table_for_01070_exception_code_in_query_log_table (value UInt64) ENGINE=Memory();
SELECT * FROM test_table_for_01070_exception_code_in_query_log_table;
SYSTEM FLUSH LOGS query_log;
SELECT exception_code FROM system.query_log WHERE current_database = currentDatabase() AND lower(query) LIKE lower('SELECT * FROM test_table_for_01070_exception_code_in_query_log_table%') AND event_date >= yesterday() AND event_time > now() - INTERVAL 5 MINUTE ORDER BY exception_code;
DROP TABLE IF EXISTS test_table_for_01070_exception_code_in_query_log_table;
