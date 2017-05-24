\set VERBOSITY terse

SET search_path = 'public';
CREATE EXTENSION pg_pathman;
CREATE SCHEMA test_exprs;


/* We use this rel to check 'pathman_hooks_enabled' */
CREATE TABLE test_exprs.canary(val INT4 NOT NULL);
CREATE TABLE test_exprs.canary_copy (LIKE test_exprs.canary);
SELECT create_hash_partitions('test_exprs.canary', 'val', 5);



/*
 * Test HASH
 */

CREATE TABLE test_exprs.hash_rel (
	id		SERIAL PRIMARY KEY,
	value	INTEGER,
	value2  INTEGER
);
INSERT INTO test_exprs.hash_rel (value, value2)
	SELECT val, val * 2 FROM generate_series(1, 5) val;

SELECT COUNT(*) FROM test_exprs.hash_rel;



\set VERBOSITY default

/* Try using constant expression */
SELECT create_hash_partitions('test_exprs.hash_rel', '1 + 1', 4);

/* Try using multiple queries */
SELECT create_hash_partitions('test_exprs.hash_rel',
							  'value, (select oid from pg_class limit 1)',
							  4);

/* Try using mutable expression */
SELECT create_hash_partitions('test_exprs.hash_rel', 'random()', 4);

/* Try using broken parentheses */
SELECT create_hash_partitions('test_exprs.hash_rel', 'value * value2))', 4);

/* Try using missing columns */
SELECT create_hash_partitions('test_exprs.hash_rel', 'value * value3', 4);

/* Check that 'pathman_hooks_enabled' is true (1 partition in plan) */
EXPLAIN (COSTS OFF) INSERT INTO test_exprs.canary_copy
SELECT * FROM test_exprs.canary WHERE val = 1;

\set VERBOSITY terse


SELECT create_hash_partitions('test_exprs.hash_rel', 'value * value2', 4);
SELECT COUNT(*) FROM ONLY test_exprs.hash_rel;
SELECT COUNT(*) FROM test_exprs.hash_rel;

INSERT INTO test_exprs.hash_rel (value, value2)
	SELECT val, val * 2 FROM generate_series(6, 10) val;
SELECT COUNT(*) FROM ONLY test_exprs.hash_rel;
SELECT COUNT(*) FROM test_exprs.hash_rel;

EXPLAIN (COSTS OFF) SELECT * FROM test_exprs.hash_rel WHERE value = 5;
EXPLAIN (COSTS OFF) SELECT * FROM test_exprs.hash_rel WHERE (value * value2) = 5;



/*
 * Test RANGE
 */

CREATE TABLE test_exprs.range_rel (id SERIAL PRIMARY KEY, dt TIMESTAMP, txt TEXT);

INSERT INTO test_exprs.range_rel (dt, txt)
SELECT g, md5(g::TEXT) FROM generate_series('2015-01-01', '2020-04-30', '1 month'::interval) as g;



\set VERBOSITY default

/* Try using constant expression */
SELECT create_range_partitions('test_exprs.range_rel', '''16 years''::interval',
							   '15 years'::INTERVAL, '1 year'::INTERVAL, 10);

/* Try using mutable expression */
SELECT create_range_partitions('test_exprs.range_rel', 'RANDOM()',
							   '15 years'::INTERVAL, '1 year'::INTERVAL, 10);

/* Check that 'pathman_hooks_enabled' is true (1 partition in plan) */
EXPLAIN (COSTS OFF) INSERT INTO test_exprs.canary_copy
SELECT * FROM test_exprs.canary WHERE val = 1;

\set VERBOSITY terse


SELECT create_range_partitions('test_exprs.range_rel', 'AGE(dt, ''2000-01-01''::DATE)',
							   '15 years'::INTERVAL, '1 year'::INTERVAL, 10);
INSERT INTO test_exprs.range_rel_1 (dt, txt) VALUES ('2020-01-01'::DATE, md5('asdf'));
SELECT COUNT(*) FROM test_exprs.range_rel_6;
INSERT INTO test_exprs.range_rel_6 (dt, txt) VALUES ('2020-01-01'::DATE, md5('asdf'));
SELECT COUNT(*) FROM test_exprs.range_rel_6;
EXPLAIN (COSTS OFF) SELECT * FROM test_exprs.range_rel WHERE (AGE(dt, '2000-01-01'::DATE)) = '18 years'::interval;

SELECT create_update_triggers('test_exprs.range_rel');
SELECT COUNT(*) FROM test_exprs.range_rel;
SELECT COUNT(*) FROM test_exprs.range_rel_1;
SELECT COUNT(*) FROM test_exprs.range_rel_2;
UPDATE test_exprs.range_rel SET dt = '2016-12-01' WHERE dt >= '2015-10-10' AND dt <= '2017-10-10';

/* counts in partitions should be changed */
SELECT COUNT(*) FROM test_exprs.range_rel;
SELECT COUNT(*) FROM test_exprs.range_rel_1;
SELECT COUNT(*) FROM test_exprs.range_rel_2;


DROP SCHEMA test_exprs CASCADE;
DROP EXTENSION pg_pathman;
