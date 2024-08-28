#include <windows.h>
#include <codecvt>
#include <locale>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <chrono>
#include <thread>
#include <conio.h>  // Для _kbhit() и _getch()
#include <stdexcept>  // Для std::invalid_argument и std::out_of_range
#include <sys/stat.h>

namespace fs = std::filesystem;

// Структура для хранения информации о пути и времени жизни файлов
struct PathConfig {
    std::string path;
    int period;
};

// Структура для хранения всех параметров конфигурации
struct Config {
    std::vector<PathConfig> paths;
    int scan_period_sec;

    Config(){ // инициализация по умолчанию
        scan_period_sec = 1;
    }
};

// Преобразует строку в целое, возвращает признак успешности
bool string_to_int(const std::string & str, int& number) {
    try {
        size_t pos;
        number = std::stoi(str, &pos);

        // Проверяем, что вся строка была преобразована
        // При этом завершающие пробелы не учитываем
        while (pos < str.length()){
            if (str[pos] > ' ') return false;
            pos++;
        }
        return true;
    }
    catch (const std::invalid_argument& e) {
        // Строка не является числом
        return false;
    }
    catch (const std::out_of_range& e) {
        // Число выходит за пределы диапазона типа int
        return false;
    }
}

// Функция для удаления пробелов с начала и конца строки
std::string trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\n\r\f\v"); // Находим первый непустой символ
    auto end = str.find_last_not_of(" \t\n\r\f\v");   // Находим последний непустой символ

    if (start == std::string::npos || end == std::string::npos) {
        return ""; // Если строка пуста или содержит только пробелы
    }

    return str.substr(start, end - start + 1); // Возвращаем подстроку без пробелов
}

// Функция для чтения конфигурации из файла. verbose - указывает, можно ли печатать сообщения
Config readConfig(const std::string& filePath, bool verbose = false) {
    Config config;

    std::ifstream configFile(filePath);

    if (!configFile.is_open()) {
        std::cerr << u8"Не могу открыть файл конфигурации: " << filePath << std::endl;
        return config;
    }

    std::string line;
    
    while (std::getline(configFile, line)) {

        // Разбор строки

        // Находим позицию символа '='
        size_t equal_pos = line.find('=');

        // Находим позицию последней запятой
        size_t comma_pos = line.rfind(',');

        // Если символ '=' найден
        if (equal_pos != std::string::npos) {
            // Получаем части строки
            std::string left = trim(line.substr(0, equal_pos)); // Часть до '='

            std::string middle;
            std::string right = "";

            if (comma_pos != std::string::npos) {
                middle = trim(line.substr(equal_pos + 1, comma_pos - equal_pos - 1)); // Часть между '=' и последней запятой
                right = trim(line.substr(comma_pos + 1)); // Часть после последней запятой
            }
            else {
                middle = trim(line.substr(equal_pos + 1)); // все после '='
            }

            if (line.find("scan_period_sec") != std::string::npos) {
                if (!string_to_int(middle, config.scan_period_sec)) {
                    if (verbose) std::cout << u8"Вероятно, ошибка в задании параметра scan_period_sec..." << std::endl;
                    continue;
                }
                if (verbose) std::cout << "scan_period_sec = " << config.scan_period_sec << std::endl;
            }
            else if (line.find("path") != std::string::npos) {
                int period = 0; // период по умолчанию


                if (!string_to_int(right, period)) {
                    if (verbose) std::cout << u8"Вероятно, ошибка в задании периода для пути " << middle << u8". По умолчанию задан период в 1 неделю" << std::endl;
                }
                config.paths.push_back({ middle, period });
                if (verbose) std::cout << middle << ", t=" << period << std::endl;

            }
            
        }
    }

    return config;
}


// Преобразование std::string (UTF-8) в std::wstring (UTF-16)
std::wstring stringToWstring(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

// Преобразование std::wstring (UTF-16) в std::string (UTF-8)
std::string WstringToString(const std::wstring& wstr) {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

std::vector<std::string> findFiles(const std::string & b_mask) {
    /*
        Функции поиска файло требуют использования wstring. 
    */
    const std::wstring& mask = stringToWstring(b_mask); 

    // Определяем полный путь к маске
    std::wstring path;
    if (mask.find(L":/") != std::wstring::npos || mask.find(L"/") == 0) {
        // Абсолютный путь
        path = std::filesystem::path(mask).parent_path().wstring() + L"/";
    }
    else {
        // Относительный путь
        std::wstring currentDir(256, L'\0');// Определяем текущую рабочую директорию
        DWORD size = GetCurrentDirectoryW(currentDir.size(), &currentDir[0]);
        if (size > 0) {
            currentDir.resize(size);
        }
        path = currentDir + L"/";
    }

    std::vector<std::string> files; // список файлов с полными путями
    WIN32_FIND_DATAW findFileData;
    
    // Определяем итератор со списком удовлетворяющих маске файлов
    HANDLE hFind = FindFirstFileW(mask.c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        auto err = GetLastError();
        // если огибка связана не с отсутствием файлов - сообщаем об ощибке
        if (err > 3) std::wcerr << u8"Ошибка шаблона в пути " << mask << u8". Код ошибки " << err << std::endl; // используется для отладки
        
    }
    else {
        do { // формируем полный путь к файлу, и сохраняем
            std::wstring fullPath = path + findFileData.cFileName;
            files.push_back(WstringToString(fullPath));
        } while (FindNextFileW(hFind, &findFileData) != 0);
        FindClose(hFind);
    }

    return files;
}

// Функция сравнивает время с последнего изменения файла с заданным количеством дней
bool isOlderThan(const std::string& filePath, int days) {
    try {
        if (days == 0) return true; // нулевой период предполагает безусловное удаление

        // Получаем время последней записи файла
        auto fileTime = fs::last_write_time(filePath);

        // Преобразуем его в системное время
        auto fileTimeSys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

        // Получаем текущее время
        auto currentTime = std::chrono::system_clock::now();

        // Вычисляем разницу
        auto fileAge = std::chrono::duration_cast<std::chrono::seconds>(currentTime - fileTimeSys).count();

        // Определяем пороговый возраст
        auto maxAge = std::chrono::seconds(86400 * days);

        return fileAge > maxAge.count();
    }
    catch (const std::exception& e) { // надеюсь, что для отладки
        std::cerr << u8"ошибка: " << e.what() << '\n';
        return false;
    }
}

// Функция для удаления файлов и директорий
void deleteFileOrDirectory(const std::string& path) {
    if (fs::is_directory(path)) {
        fs::remove_all(path);
    }
    else {
        fs::remove(path);
    }
}

// Функция для обработки команды выхода
bool checkForExitCommand() {
    if (_kbhit()) { // Проверяем, есть ли ввод от пользователя
        char ch = _getch(); // Получаем символ от пользователя
        if (ch == 24) { // Код для Ctrl+X
            return true;
        }
        else if (ch == 'e' || ch == 'E') { // Проверяем команду exit
            std::string command;
            std::getline(std::cin, command);
            if (command == "xit" || command == "EXIT" || command == "exit") {
                return true;
            }
        }
    }
    return false;
}

// Главная функция программы
int main(int argc, char* argv[]) {
    // Установка кодировки консоли на UTF-8
    SetConsoleOutputCP(CP_UTF8);

    std::string configFilePath = "config.ini";  // Имя файла конфигурации по умолчанию

    // Если путь до файла конфигурации передан через аргументы командной строки
    if (argc > 1) {
        configFilePath = argv[1];
    }

    bool print = true; // разрешение на вывод сообщений о разборе ini файла. Устанавливается при первом проходе
    bool toExit = false; // признак завершения

    while (!toExit) {
        Config config = readConfig(configFilePath, print /*при первом вызове печатаем прочитанные настройки*/);
        print = false;

        if (config.paths.size() == 0) {
            std::cout << u8"Ошибка чтения ini файла";
            break;
        }


        // Поиск и удаление файлов по конфигурации
        for (const auto& pathConfig : config.paths) {
            std::vector<std::string> files = findFiles(pathConfig.path);
            for (const auto& file : files) {
                if (isOlderThan(file, pathConfig.period)) {
                    deleteFileOrDirectory(file);
                    std::cout << u8"Удаляем: " << file << std::endl;
                }
            }
        }

        // ожидаю ввода с клавиатуры команды на выход
        for (int N = 0; N < config.scan_period_sec * 20; N++) {
            // Проверка на команду выхода
            if (checkForExitCommand()) {
                std::cout << u8"Выход..." << std::endl;
                toExit = true;
                break;
            }

            // останавливаем поток
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    return 0;
}
