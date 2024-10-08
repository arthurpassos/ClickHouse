-- Tags: replica, distributed

SET enable_parallel_replicas = 1;
SET parallel_replicas_mode = 'sampling_key';
SET max_parallel_replicas = 2;
SET parallel_replicas_for_non_replicated_merge_tree = 1;
DROP TABLE IF EXISTS report;

set allow_deprecated_syntax_for_merge_tree=1;
CREATE TABLE report(id UInt32, event_date Date, priority UInt32, description String) ENGINE = MergeTree(event_date, intHash32(id), (id, event_date, intHash32(id)), 8192);

INSERT INTO report(id,event_date,priority,description) VALUES (1, '2015-01-01', 1, 'foo')(2, '2015-02-01', 2, 'bar')(3, '2015-03-01', 3, 'foo')(4, '2015-04-01', 4, 'bar')(5, '2015-05-01', 5, 'foo');
SELECT * FROM (SELECT id, event_date, priority, description FROM remote('127.0.0.{2|3}', currentDatabase(), report)) ORDER BY id ASC;

DROP TABLE report;
