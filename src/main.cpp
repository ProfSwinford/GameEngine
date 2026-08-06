#include <print>

int main() {
    // Prints without a newline
    std::print("Hello "); 

    // Prints and automatically appends a newline
    std::println("World!"); 
    
    int age = 25;
    std::string name = "Prof. Swinford";
    
    // Using curly braces as placeholders
    std::println("My name is {} and I am {} years old.", name, age);
}

