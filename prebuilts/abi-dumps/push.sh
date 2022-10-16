#!/bin/bash

for i in `ls`; do 
	git add $i;
	git commit -m "push $i";
	git push -u origin master;
done
