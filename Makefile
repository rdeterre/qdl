OUT := qdl

CFLAGS := -O2 -Wall -g `xml2-config --cflags` `pkg-config --cflags libusb-1.0`
LDFLAGS := `xml2-config --libs` `pkg-config --libs libusb-1.0`
prefix := /usr/local

SRCS := firehose.c qdl.c sahara.c util.c patch.c program.c ufs.c qdl_main.c
OBJS := $(SRCS:.c=.o)

$(OUT): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OUT) $(OBJS)

install: $(OUT)
	install -D -m 755 $< $(DESTDIR)$(prefix)/bin/$<
