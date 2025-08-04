noinst_LIBRARIES = libsqldb.a
ARFLAGS = rv

libsqldb_a_SOURCES = \
	sqldb.cpp \
	sqldrvpg.cpp

# Шляхи до заголовків, від яких залежить ця бібліотека
# В основному, це libkycore для доступу до rack.h
AM_CPPFLAGS = \
	-I$(top_srcdir)/src/libkycore