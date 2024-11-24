#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdbool.h>

#define SIZE 10
#define FILEPATH "game_log.txt"
#define FILESIZE 10000

// Global variables
char board[SIZE][SIZE];
pthread_mutex_t board_mutex;
pthread_mutex_t log_mutex;
bool game_over = false;
int submarine_parts = 0; // Remaining submarine parts on the board
int file_descriptor;
char *file_memory;

// Initializes the game board with empty cells
void initialize_board()
{
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            board[i][j] = '.';
        }
    }
}

// Places submarines randomly on the board
void placing_submarines()
{
    int total_cells = SIZE * SIZE;
    // random number of submarine cells (10%-15% of the board)
    int submarine_cells = rand() % ((int)(0.15 * total_cells) - (int)(0.1 * total_cells) + 1) + (int)(0.1 * total_cells);

    while (submarine_parts < submarine_cells)
    {
        int length = (rand() % 3) + 2; // Submarine length: 2-4 cells
        int row = rand() % SIZE;
        int col = rand() % SIZE;
        int direction = rand() % 2; // 0 = horizontal, 1 = vertical
        bool valid = true;

        // Check if the placement is valid
        for (int i = 0; i < length; i++)
        {
            int r = row + (direction == 1 ? i : 0);
            int c = col + (direction == 0 ? i : 0);
            if (r >= SIZE || c >= SIZE || board[r][c] == 'X') // Check bounds and collision
            {
                valid = false;
                break;
            }
        }

        // Place the submarine if the position is valid
        if (valid)
        {
            for (int i = 0; i < length; i++)
            {
                int r = row + (direction == 1 ? i : 0);
                int c = col + (direction == 0 ? i : 0);
                board[r][c] = 'X'; // 'X' represents a submarine part
            }
            submarine_parts += length; // Update the count of submarine parts
        }
    }
}

// Writes the initial state of the board to the log file
void write_initial_board_to_file()
{
    pthread_mutex_lock(&log_mutex); // Ensure exclusive access to the log file

    char board_state[2048];
    int offset = 0;

    // Format the board into a string
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            offset += snprintf(board_state + offset, sizeof(board_state) - offset, " %c ", board[i][j]);
        }
        offset += snprintf(board_state + offset, sizeof(board_state) - offset, "\n");
    }
    offset += snprintf(board_state + offset, sizeof(board_state) - offset, "\n");

    // Write to the memory-mapped file if there's enough space
    if (offset < FILESIZE)
    {
        strncpy(file_memory, board_state, offset);
        file_memory[offset] = '\0'; // Null-terminate the string
    }

    pthread_mutex_unlock(&log_mutex); // Release the mutex
}

// Writes a message to the log file without overwriting existing content
void write_log_to_file(const char *message)
{
    pthread_mutex_lock(&log_mutex);

    int current_length = strlen(file_memory);
    int message_length = strlen(message);

    // Append the message if there's enough space
    if (current_length + message_length < FILESIZE)
    {
        strncat(file_memory, message, FILESIZE - current_length - 1);
        file_memory[current_length + message_length] = '\0'; // Null-terminate the string
    }

    pthread_mutex_unlock(&log_mutex);
}

// Appends the final board state to the log file
void append_final_board_to_file()
{
    pthread_mutex_lock(&log_mutex);

    int current_length = strlen(file_memory);
    char board_state[2048];
    int offset = 0;

    // Format the board into a string
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            offset += snprintf(board_state + offset, sizeof(board_state) - offset, " %c ", board[i][j]);
        }
        offset += snprintf(board_state + offset, sizeof(board_state) - offset, "\n");
    }

    // Append to the memory-mapped file if there's enough space
    if (current_length + offset < FILESIZE)
    {
        strncat(file_memory, board_state, FILESIZE - current_length - 1);
        file_memory[current_length + offset] = '\0'; // Null-terminate the string
    }

    pthread_mutex_unlock(&log_mutex);
}

// Prints the board to the console
void print_board()
{
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            printf(" %c ", board[i][j]);
        }
        printf("\n");
    }
    printf("\n");
}

// Checks if the game is over (all submarines are destroyed)
bool check_game_over()
{
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            if (board[i][j] == 'X')
            {
                return false;
            }
        }
    }
    return true;
}

// Thread function- representing a player
void *player(void *arg)
{
    int thread_id = *((int *)arg);
    free(arg); // Free allocated memory for thread ID

    while (!game_over)
    {
        pthread_mutex_lock(&board_mutex);

        // Check if the game has ended
        if (game_over)
        {
            pthread_mutex_unlock(&board_mutex);
            break;
        }

        // Randomly select a cell to attack
        int row = rand() % SIZE;
        int col = rand() % SIZE;
        char result_message[256];

        if (board[row][col] == 'X') // Hit a submarine part
        {
            board[row][col] = 'V'; // Mark as hit
            snprintf(result_message, sizeof(result_message),
                     "Thread %d: Found a part of a submarine at [%d,%d]\n",
                     thread_id, row, col);
            submarine_parts--;
        }
        else if (board[row][col] == '.') // Missed
        {
            snprintf(result_message, sizeof(result_message),
                     "Thread %d: Missed at [%d,%d]\n",
                     thread_id, row, col);
        }
        else // Already attacked cell
        {
            snprintf(result_message, sizeof(result_message),
                     "Thread %d: Skipped at [%d,%d]\n",
                     thread_id, row, col);
        }

        // Print and log result
        printf("%s", result_message);
        write_log_to_file(result_message);

        // Update  game status
        game_over = check_game_over();

        pthread_mutex_unlock(&board_mutex);

        sleep(1); // Pause for 1 second
    }

    return NULL;
}

// Main function
int main()
{
    pthread_t threads[3];
    srand(time(NULL));

    // Initialize the game board and place submarines
    initialize_board();
    placing_submarines();

    pthread_mutex_init(&board_mutex, NULL);
    pthread_mutex_init(&log_mutex, NULL);

    // Create and prepare a memory-mapped file for logging
    file_descriptor = open(FILEPATH, O_RDWR | O_CREAT, 0666);
    ftruncate(file_descriptor, FILESIZE);
    file_memory = mmap(0, FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor, 0);
    memset(file_memory, 0, FILESIZE);

    // Log and print initial board state
    write_initial_board_to_file();
    print_board();

    // Create threads
    for (int i = 0; i < 3; i++)
    {
        int *id = malloc(sizeof(int));                 // Allocate memory for thread ID
        *id = i + 1;                                   // Assign thread ID
        pthread_create(&threads[i], NULL, player, id); // Start thread
    }

    // Wait for threads to finish
    for (int i = 0; i < 3; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("Game over! All submarines have been destroyed.\n");
    write_log_to_file("Game over! All submarines have been destroyed.\n");

    // Log and final board state
    append_final_board_to_file();
    print_board();

    // Clean up
    pthread_mutex_destroy(&board_mutex);
    pthread_mutex_destroy(&log_mutex);
    munmap(file_memory, FILESIZE);
    close(file_descriptor);

    return 0;
}