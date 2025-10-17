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
#### Linux / MAC:
```sh
git clone https://github.com/Stephen-Duffy-SunyPoly/terminal-snake-multiplayer
cd terminal-snake
g++ -o snake.game -pthread src/snake.cpp -std=c++17
# You may wish to run `reset` after the game exits to fix your terminals printing
```


#### Windows:
with visual c++ compiler
```cmd
git clone https://github.com/Stephen-Duffy-SunyPoly/terminal-snake-multiplayer
cd terminal-snake
cl src/snake.cpp
```
