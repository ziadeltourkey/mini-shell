myshell: command.o tokenizer.o
	g++ -Wall -Wextra -g -o myshell command.o tokenizer.o

command.o: command.cc command.h tokenizer.h
	g++ -Wall -Wextra -g -c command.cc

tokenizer.o: tokenizer.cc tokenizer.h
	g++ -Wall -Wextra -g -c tokenizer.cc

clean:
	rm -f *.o myshell log.txt
