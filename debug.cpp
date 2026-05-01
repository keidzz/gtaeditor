#include <iostream>
#include <fstream>
#include <string>

int main() {
    std::ifstream file("data/maps/la/lae2.ide");
    if (!file) { std::cout << "no file" << std::endl; return 0; }
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("hous") != std::string::npos) {
            std::cout << line << std::endl;
        }
    }
    return 0;
}
