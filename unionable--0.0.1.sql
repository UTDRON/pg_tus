\echo Use "CREATE EXTENSION unionable" to load this file. \quit
CREATE OR REPLACE FUNCTION unionableFindTopK(text, integer) RETURNS text
AS '$libdir/unionable', 'unionableFindTopK' 
LANGUAGE C IMMUTABLE STRICT;


CREATE OR REPLACE FUNCTION create_encoding() 
RETURNS text
AS '$libdir/unionable', 'create_encoding' 
LANGUAGE C VOLATILE
SECURITY DEFINER;