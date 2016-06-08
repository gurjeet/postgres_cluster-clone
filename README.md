# pg_tsparser - parser for text search

## Introduction

The **pg_tsparser** module is the modified default text search parser from
PostgreSQL 9.6.

## License

This module available under the same license as
[PostgreSQL](http://www.postgresql.org/about/licence/).

## Installation

Typical installation procedure may look like this:

    $ cd pg_tsparser
    $ sudo make USE_PGXS=1 install
    $ make USE_PGXS=1 installcheck
    $ psql DB -c "CREATE EXTENSION pg_tsparser;"

After this you can create your own text search configuration:

```sql
CREATE TEXT SEARCH CONFIGURATION russian_ts (
    PARSER = tsparser
);

COMMENT ON TEXT SEARCH CONFIGURATION russian_ts IS 'text search configuration for russian language';

ALTER TEXT SEARCH CONFIGURATION russian_ts
    ADD MAPPING FOR email, file, float, host, hword_numpart, int,
    numhword, numword, sfloat, uint, url, url_path, version
    WITH simple;

ALTER TEXT SEARCH CONFIGURATION russian_ts
    ADD MAPPING FOR asciiword, asciihword, hword_asciipart
    WITH russian_stem, english_stem;

ALTER TEXT SEARCH CONFIGURATION russian_ts
    ADD MAPPING FOR word, hword, hword_part
    WITH russian_stem, russian_stem;
```
