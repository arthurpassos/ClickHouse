SELECT 1 FROM numbers(1) WHERE toIntervalHour(number) = 0;

CREATE TABLE t1 (c0 Decimal(18,0)) ENGINE = MergeTree() ORDER BY (c0);
INSERT INTO TABLE t1(c0) VALUES (1);

SELECT c0 = 6812671276462221925::Int64 FROM t1;
SELECT 1 FROM t1 WHERE c0 = 6812671276462221925::Int64;
