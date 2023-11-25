src = $(wildcard *.c)
obj = $(src:.c=.o)

CFLAGS = -Og -g
#-std=gnu99
LDFLAGS = -lpthread

q: $(obj)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) q

