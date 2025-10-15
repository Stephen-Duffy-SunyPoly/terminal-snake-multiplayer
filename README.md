# snake
a simple game of snake that can be played in your computer terminal

### controls:
arrow keys/ WASD: change direction  
q: quit   
p: pause   
h: display help text  

### used libraries: 
[Rogueutil](https://github.com/sakhmatd/rogueutil)

### build instruction:
#### linux:
```sh
git clone https://github.com/jSdCool/terminal-snake
cd terminal-snake
g++ -o snake.game -pthread src/snake.cpp
```
#### windows:
```cmd
git clone https://github.com/jSdCool/terminal-snake
cd terminal-snake
g++ -o snake.exe src/snake.cpp
```
or with visual c++ compiler
```cmd
git clone https://github.com/jSdCool/terminal-snake
cd terminal-snake
cl src/snake.cpp
```
