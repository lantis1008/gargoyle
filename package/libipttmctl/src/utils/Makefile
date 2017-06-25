all: tm_set tm_get tm_print_history_file
tm_set: tm_set.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@  $^ -lipttmctl
tm_get: tm_get.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@  $^ -lipttmctl
tm_print_history_file: tm_print_history_file.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@  $^ -lipttmctl

clean:
	rm -rf tm_set tm_get print_history_file *.o *~ .*sw*