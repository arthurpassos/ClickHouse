-- Tags: stateful
SELECT URL, EventDate, max(URL) FROM test.hits WHERE CounterID = 1704509 AND UserID = 4322253409885123546 GROUP BY URL, EventDate, EventDate ORDER BY URL, EventDate;
