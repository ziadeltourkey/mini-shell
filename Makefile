myshell: command.o tokenizer.o #da default target
	g++ -Wall -Wextra -g -o myshell command.o tokenizer.o 

command.o: command.cc command.h tokenizer.h # hna b compile elshell logic wn7wlo l obect file
	g++ -Wall -Wextra -g -c command.cc

tokenizer.o: tokenizer.cc tokenizer.h # hna b2a b compile brdo bs ll tokenizer l object file
	g++ -Wall -Wextra -g -c tokenizer.cc

clean: # btms7 el object files
	rm -f *.o myshell log.txt
