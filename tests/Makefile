CC = gcc
CFLAGS = -Wall -I../src -pthread -O0 -g 
LDFLAGS =  -lrt -lcheck -lsubunit -lm 
SRC = ../src/DataHub.c
INCLUDE = -I../include

UNIT_TEST = test_unit
STRESS_TEST = test_stress
DEMO = test_demo

.PHONY: all clean

all: $(UNIT_TEST) $(STRESS_TEST) $(DEMO)

$(UNIT_TEST): test_datahub_unit_check.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(INCLUDE) $(SRC) $(LDFLAGS)

$(STRESS_TEST): test_datahub_stress_check.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(INCLUDE) $(SRC) $(LDFLAGS)

$(DEMO): app_industrial_demo.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(INCLUDE) $(SRC) $(LDFLAGS)

clean:
	rm -f $(UNIT_TEST) $(STRESS_TEST) $(DEMO)

