# Target library
lib := libfs.a
objs := disk.o fs.o


CC := gcc
CFLAGS := -Wall -Wextra -MMD

ifneq ($(V),1)
Q = @
endif

all: $(lib)

deps := $(patsubst %.o,%.d,$(objs))
-include $(deps)

%.o: %.c
	@echo "CC $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(lib): $(objs)
	@echo "AR RCS $@"
	$(Q)ar rcs $@ $(objs)

clean:
	@echo "CLEAN"
	$(Q)@rm -f $(lib) $(objs) $(deps)

