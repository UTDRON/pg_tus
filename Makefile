EXTENSION = unionable
DATA = unionable--0.0.1.sql
REGRESS = unionable_test

OBJS = unionable.o utils.o
MODULE_big = unionable

# MODULES = unionable
# HEADERS_unionable = utils.h

PG_CONFIG = pg_config

PG_CFLAGS = -I/opt/homebrew/Cellar/postgresql@14/14.12/include/postgresql@14
PG_LDFLAGS = -L/opt/homebrew/Cellar/postgresql@14/14.12/lib -lpq

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)