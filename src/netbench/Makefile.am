AM_CPPFLAGS = $(CXX17_FLAGS) $(SSL_CFLAGS)  -I$(srcdir)/.. -I$(srcdir)/../util -I$(srcdir)/../http
AM_CXXFLAGS = $(PICKY_CXXFLAGS)

LAMBDA_FLAGS = -ldl -static -Wl,--whole-archive -lpthread -Wl,--no-whole-archive

bin_PROGRAMS = worker master

master_SOURCES = master.cc
master_LDADD = ../http/libmushhttp.a ../util/libmushutil.a

worker_SOURCES = worker.cc
worker_LDADD = ../http/libmushhttp.a ../util/libmushutil.a
worker_LDFLAGS = $(LAMBDA_FLAGS)
