#include <iostream>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>
#include <fcntl.h>
#include <sys/types.h>


using namespace std;

int _SIZE;

struct Slave {
    pthread_barrier_t* barrier;
    char* text;
    char* output_text;
    char* pseudorandom_seq;
    int first_index;
    int last_index;
};

struct ARG {
    char* path_to_text;
    char* patch_to_cypher;
    int x;
    int a;
    int c;
    int m;
};

//Реализация генератора псевдослучайных чисел (конгруэнтный метод)
void* RandomNumbers(void* cmd_argv_ptr)
{
    ARG* cmd_argv = static_cast<ARG*>(cmd_argv_ptr);
    int x = cmd_argv->x;
    int a = cmd_argv->a;
    int c = cmd_argv->c;
    int m = cmd_argv->m;
    //создаем массив под рандонмные числа и устанавливаем первый элемент = x
    int count_of_int = _SIZE / sizeof(int);
    int* buff = new int[count_of_int + 1];
    buff[0] = x;
    //Заполнение массива рандомными числами по формуле (a * buff[i-1] + c) % m
    for(size_t i = 1; i < count_of_int + 1; i++)
    {
        buff[i]= (a * buff[i-1] + c) % m;
    }
    //Приведение указателя на массив к типу char*
    char* seq = reinterpret_cast<char *>(buff);
    return seq;
}

void* encode(void * slave_ptr)
{
    //Преобразование указателя и инициализация переменных данными из структуры Slave
    Slave* slave = static_cast<Slave*>(slave_ptr);
    int current_index = slave->first_index;
    int last_index = slave->last_index;
    //Шифрование
    while(current_index < last_index)
    {
        slave->output_text[current_index] = slave->pseudorandom_seq[current_index] ^ slave->text[current_index];
        current_index++;
    }
    //Блокируем поток и ждем пока все slave достигнут "контрольной точки"
    int status = pthread_barrier_wait(slave->barrier);
    if (status != 0 && status != PTHREAD_BARRIER_SERIAL_THREAD) {
        exit(status);
    }
    return nullptr;
}


int main(int argc, char* argv[])
{
    //Обработка исключения(если аргументов в КС меньше или больше 13)
    if (argc != 13)
    {
        std::cout << "Arguments error" << std::endl;
        exit(1);
    }
    int c;
    ARG cmd_argv;
    //Записываем аргументы КС в структуру
    while ((c = getopt(argc, argv, "i:o:x:a:c:m:")) != -1)
    {
        switch (c)
        {
            case 'i':
                cmd_argv.path_to_text = optarg;
                break;
            case 'o':
                cmd_argv.patch_to_cypher = optarg;
                break;
            case 'x':
                cmd_argv.x = atoi(optarg);
                break;
            case 'a':
                cmd_argv.a = atoi(optarg);
                break;
            case 'c':
                cmd_argv.c = atoi(optarg);
                break;
            case 'm':
                cmd_argv.m = atoi(optarg);
                break;
            default:
                break;
        }
    }
    //Обработка исключения (все ли было обработана)
    if (optind < argc)
    {
        std::cout << "Can't recognised elements" << std::endl;
        exit(1);
    }
    //Открываем файлик
    int input_file = open(cmd_argv.path_to_text, O_RDONLY);
    if (input_file == -1)
    {
        std::cout << "Cant Open " << cmd_argv.path_to_text << " file" << std::endl;
        exit(1);
    }

    //Смотрим размер файла, если большой, то выводим сообщение
    _SIZE = lseek(input_file, 0, SEEK_END);
    if (_SIZE > 10000)
    {
        std::cout << "Can't open file with big size"<< std:: endl;
        exit(1);
    }
    lseek(input_file, 0, SEEK_SET);

    //Если большой размер файла, то выделить память не получится
    char* text = new char[_SIZE];
    if(read(input_file, text, _SIZE) == -1)
    {
        std::cout << "Can't load file to RAM" << std::endl;
        exit(1);
    }

    pthread_t keygen_thread;
    //Создаем поток keygen_thread со стандартными атрибутами, который будет выполнять RandomNumbers()
    if (pthread_create(&keygen_thread, NULL, RandomNumbers(), &cmd_argv) != 0)
    {
        std::cout << "Cant create a keygen thread" << std::endl;
        exit(1);
    }
    //Блокируем поток, пока не завершится keygen_thread
    char* pseudorandom_seq = nullptr;
    if(pthread_join(keygen_thread, (void**)&pseudorandom_seq))
    {
        std::cout << "Cant join a keygen thread" << std::endl;
        exit(1);
    }

    pthread_barrier_t barrier;


    int number_of_processors = sysconf(_SC_NPROCESSORS_ONLN);

    pthread_barrier_init(&barrier, NULL, number_of_processors + 1);
    pthread_t crypt_threads[number_of_processors];
    std::vector <Slave*> slaves;

    size_t part_len = _SIZE / number_of_processors;
    if (_SIZE % number_of_processors != 0) {
        part_len ++;
    }

    //Создать потоки и запустить на них шифрование
    //Каждый поток обрабатывает свой кусок, после все записывается в общее хранилище
    char* output_text = new char[_SIZE];
    for(int i = 0; i < number_of_processors; i++)
    {
        Slave* slave = new Slave;

        slave->barrier = &barrier;
        slave->text = text;
        slave->output_text = output_text;
        slave->pseudorandom_seq = pseudorandom_seq;
        slave->first_index = i * part_len;

        if (i == number_of_processors - 1)
            slave->last_index = _SIZE;
        else
            slave->last_index = slave->first_index + part_len;

        slaves.push_back(slave);
        pthread_create(&crypt_threads[i], NULL, encode, slave);
    }

    int status = pthread_barrier_wait(&barrier);
    if (status != 0 && status != PTHREAD_BARRIER_SERIAL_THREAD)
    {
        std::cout << "Some problems with barrier" << std::endl;
        exit(status);
    }

    int output_file = open(cmd_argv.patch_to_cypher, O_WRONLY, O_TRUNC);
    if (output_file == -1)
    {
        std::cout << "Unable to open " << cmd_argv.patch_to_cypher << " file" << std::endl;
        exit(1);
    }

    write(output_file, output_text, _SIZE);

    close(output_file);

    pthread_barrier_destroy(&barrier);

    return 0;
}

