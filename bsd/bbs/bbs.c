#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Constants */
#define MAX_OPTIONS 10
#define MAX_MENU_DEPTH 10
#define USERNAME_LEN 20
#define PASSWORD_HASH_LEN 32
#define USERS_FILE "users.dat"
#define MAX_USERS 100
#define SALT_LENGTH 8

/* MenuOption struct: Represents an option in a menu, which can lead to a function or submenu */
typedef struct {
    char *description;          /* Text displayed for the option */
    void (*function)();         /* Function to call if selected (NULL if submenu) */
    struct Menu *submenu;       /* Submenu to navigate to (NULL if function) */
} MenuOption;

/* Menu struct: Defines a menu with a title and a list of options */
typedef struct Menu {
    char *title;                /* Title of the menu */
    MenuOption options[MAX_OPTIONS]; /* Array of menu options */
    int num_options;            /* Number of options in the menu */
} Menu;

/* User struct: Holds user data */
typedef struct {
    char username[USERNAME_LEN];
    char password_hash[PASSWORD_HASH_LEN];
    /* Additional fields like real name could be added here */
} User;

/* Global variables */
Menu main_menu;                 /* Main menu of the BBS */
Menu message_boards_menu;       /* Submenu for message boards */
Menu file_transfers_menu;       /* Submenu for file transfers */
Menu online_games_menu;         /* Submenu for online games */
Menu user_profile_menu;         /* Submenu for user profile management */
User *users;                    /* Array of registered users */
int num_users;                  /* Number of users */
User current_user;              /* Currently logged-in user */
int logged_in = 0;              /* Login status flag */
Menu *menu_stack[MAX_MENU_DEPTH]; /* Stack for menu navigation */
int top = -1;                   /* Top index of the menu stack */

/* Function prototypes */
void init_system(void);
void handle_login(void);
void menu_loop(void);
void display_menu(Menu *menu);
char get_user_input(void);
void logout(void);
void register_new_user(void);
void view_message_boards(void);
void file_transfers(void);
void online_games(void);
void view_profile(void);
void edit_profile(void);
void load_users(void);
void save_users(void);
int find_user(const char *username);
void hash_password(const char *password, char *hashed);
int verify_password(const char *password, const char *hashed);

/* Menu stack management */
void push_menu(Menu *menu) {
    if (top < MAX_MENU_DEPTH - 1) {
        menu_stack[++top] = menu;
    }
}

Menu *pop_menu(void) {
    if (top >= 0) {
        return menu_stack[top--];
    }
    return NULL;
}

/* Main function: Entry point of the BBS */
int main(void) {
    init_system();
    handle_login();
    if (logged_in) {
        menu_loop();
    }
    return 0;
}

/* init_system: Initializes the BBS system, including menus and user data */
void init_system(void) {
    /* Initialize main menu */
    main_menu.title = "Main Menu";
    main_menu.num_options = 5;
    main_menu.options[0].description = "Message Boards";
    main_menu.options[0].submenu = &message_boards_menu;
    main_menu.options[0].function = NULL;
    main_menu.options[1].description = "File Transfers";
    main_menu.options[1].submenu = &file_transfers_menu;
    main_menu.options[1].function = NULL;
    main_menu.options[2].description = "Online Games";
    main_menu.options[2].submenu = &online_games_menu;
    main_menu.options[2].function = NULL;
    main_menu.options[3].description = "User Profile";
    main_menu.options[3].submenu = &user_profile_menu;
    main_menu.options[3].function = NULL;
    main_menu.options[4].description = "Logout";
    main_menu.options[4].submenu = NULL;
    main_menu.options[4].function = logout;

    /* Initialize message boards menu */
    message_boards_menu.title = "Message Boards";
    message_boards_menu.num_options = 4;
    message_boards_menu.options[0].description = "General Discussion";
    message_boards_menu.options[0].submenu = NULL;
    message_boards_menu.options[0].function = view_message_boards;
    message_boards_menu.options[1].description = "Tech Talk";
    message_boards_menu.options[1].submenu = NULL;
    message_boards_menu.options[1].function = view_message_boards;
    message_boards_menu.options[2].description = "Post New Message";
    message_boards_menu.options[2].submenu = NULL;
    message_boards_menu.options[2].function = post_message;
    message_boards_menu.options[3].description = "Search Messages";
    message_boards_menu.options[3].submenu = NULL;
    message_boards_menu.options[3].function = search_messages;

    /* Initialize file transfers menu */
    file_transfers_menu.title = "File Transfers";
    file_transfers_menu.num_options = 4;
    file_transfers_menu.options[0].description = "Upload File";
    file_transfers_menu.options[0].submenu = NULL;
    file_transfers_menu.options[0].function = upload_file;
    file_transfers_menu.options[1].description = "Download File";
    file_transfers_menu.options[1].submenu = NULL;
    file_transfers_menu.options[1].function = download_file;
    file_transfers_menu.options[2].description = "List Files";
    file_transfers_menu.options[2].submenu = NULL;
    file_transfers_menu.options[2].function = list_files;
    file_transfers_menu.options[3].description = "Search Files";
    file_transfers_menu.options[3].submenu = NULL;
    file_transfers_menu.options[3].function = search_files;

    /* Initialize online games menu */
    online_games_menu.title = "Online Games";
    online_games_menu.num_options = 3;
    online_games_menu.options[0].description = "Hangman";
    online_games_menu.options[0].submenu = NULL;
    online_games_menu.options[0].function = play_hangman;
    online_games_menu.options[1].description = "Text Adventure";
    online_games_menu.options[1].submenu = NULL;
    online_games_menu.options[1].function = play_adventure;
    online_games_menu.options[2].description = "High Scores";
    online_games_menu.options[2].submenu = NULL;
    online_games_menu.options[2].function = view_high_scores;

    /* Initialize user profile menu */
    user_profile_menu.title = "User Profile";
    user_profile_menu.num_options = 2;
    user_profile_menu.options[0].description = "View Profile";
    user_profile_menu.options[0].submenu = NULL;
    user_profile_menu.options[0].function = view_profile;
    user_profile_menu.options[1].description = "Edit Profile";
    user_profile_menu.options[1].submenu = NULL;
    user_profile_menu.options[1].function = edit_profile;

    /* Load user data from file */
    load_users();
}

/* Initialize users array */
void load_users(void) {
    FILE *file = fopen(USERS_FILE, "rb");
    users = malloc(sizeof(User) * MAX_USERS);
    num_users = 0;

    if (file != NULL) {
        /* Read number of users */
        fread(&num_users, sizeof(int), 1, file);
        /* Read user data */
        fread(users, sizeof(User), num_users, file);
        fclose(file);
    }
}

/* Save users to file */
void save_users(void) {
    FILE *file = fopen(USERS_FILE, "wb");
    if (file != NULL) {
        fwrite(&num_users, sizeof(int), 1, file);
        fwrite(users, sizeof(User), num_users, file);
        fclose(file);
    }
}

/* Find user by username, returns index or -1 if not found */
int find_user(const char *username) {
    for (int i = 0; i < num_users; i++) {
        if (strcmp(users[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

/* Simple password hashing (in practice, use a proper crypto library) */
void hash_password(const char *password, char *hashed) {
    /* Simple XOR-based hash for demonstration - NOT FOR PRODUCTION USE */
    const char salt[] = "BBS2024!";
    int i;
    for (i = 0; i < PASSWORD_HASH_LEN - 1 && password[i] != '\0'; i++) {
        hashed[i] = password[i] ^ salt[i % SALT_LENGTH];
    }
    hashed[i] = '\0';
}

/* Verify password against stored hash */
int verify_password(const char *password, const char *hashed) {
    char computed_hash[PASSWORD_HASH_LEN];
    hash_password(password, computed_hash);
    return strcmp(computed_hash, hashed) == 0;
}

/* Updated handle_login implementation */
void handle_login(void) {
    char choice;
    char username[USERNAME_LEN];
    char password[USERNAME_LEN];
    char hashed[PASSWORD_HASH_LEN];
    
    while (!logged_in) {
        printf("\nBBS Login\n");
        printf("1. Log in\n2. Register\n3. Exit\n");
        choice = get_user_input();
        
        if (choice == '1') {
            printf("Username: ");
            scanf("%s", username);
            printf("Password: ");
            scanf("%s", password);
            
            int user_index = find_user(username);
            if (user_index >= 0) {
                hash_password(password, hashed);
                if (verify_password(password, users[user_index].password_hash)) {
                    logged_in = 1;
                    current_user = users[user_index];
                    printf("Welcome back, %s!\n", username);
                } else {
                    printf("Invalid password.\n");
                }
            } else {
                printf("User not found.\n");
            }
        }
        else if (choice == '2') {
            register_new_user();
        }
        else if (choice == '3') {
            exit(0);
        }
    }
}

/* Updated register_new_user implementation */
void register_new_user(void) {
    char username[USERNAME_LEN];
    char password[USERNAME_LEN];
    char confirm_password[USERNAME_LEN];
    
    if (num_users >= MAX_USERS) {
        printf("Maximum number of users reached.\n");
        return;
    }
    
    printf("Enter new username: ");
    scanf("%s", username);
    
    if (find_user(username) >= 0) {
        printf("Username already exists.\n");
        return;
    }
    
    printf("Enter password: ");
    scanf("%s", password);
    printf("Confirm password: ");
    scanf("%s", confirm_password);
    
    if (strcmp(password, confirm_password) != 0) {
        printf("Passwords do not match.\n");
        return;
    }
    
    /* Create new user */
    strcpy(users[num_users].username, username);
    hash_password(password, users[num_users].password_hash);
    num_users++;
    
    /* Save updated user list */
    save_users();
    printf("Registration successful! Please log in.\n");
}

void menu_loop(void) {
    char input;
    push_menu(&main_menu);
    while (top >= 0) {
        Menu *current_menu = menu_stack[top];
        display_menu(current_menu);
        input = get_user_input();
        if (input == 'Q') {
            pop_menu();
        }
        else {
            int option_index;
            option_index = input - '1';
            if (option_index >= 0 && option_index < current_menu->num_options) {
                MenuOption *option = &current_menu->options[option_index];
                if (option->submenu != 0) {
                    push_menu(option->submenu);
                }
                else if (option->function != 0) {
                    option->function();
                }
            }
        }
    }
}

void display_menu(Menu *menu) {
    int i;
    printf("%s\n", menu->title);
    for (i = 0; i < menu->num_options; i++) {
        printf("%d. %s\n", i + 1, menu->options[i].description);
    }
    printf("Q. Return to previous menu\n");
}

/* get_user_input: Retrieves user input (simplified) */
char get_user_input(void) {
    char input;
    scanf(" %c", &input); /* Skip whitespace */
    /* TODO: Enhance input handling for robustness */
    return input;
}

/* logout: Logs out the user and exits the menu loop */
void logout(void) {
    while (top >= 0) {
        pop_menu();
    }
    printf("Logging out...\n");
    /* TODO: Perform additional cleanup (e.g., save session data) */
}

/* Stub functions for BBS features */
void view_message_boards(void) {
    printf("TODO: Implement message boards functionality\n");
    /* TODO: Display message boards and handle posting/reading */
}

void file_transfers(void) {
    printf("TODO: Implement file transfers functionality\n");
    /* TODO: Handle file uploads and downloads */
}

void online_games(void) {
    printf("TODO: Implement online games functionality\n");
    /* TODO: List and launch available games */
}

/* Updated view_profile implementation */
void view_profile(void) {
    printf("\nUser Profile\n");
    printf("Username: %s\n", current_user.username);
    /* Add additional profile fields here */
}

/* Updated edit_profile implementation */
void edit_profile(void) {
    char current_password[USERNAME_LEN];
    char new_password[USERNAME_LEN];
    char confirm_password[USERNAME_LEN];
    
    printf("\nEdit Profile\n");
    printf("1. Change Password\n");
    printf("Q. Return\n");
    
    char choice = get_user_input();
    if (choice == '1') {
        printf("Enter current password: ");
        scanf("%s", current_password);
        
        if (verify_password(current_password, current_user.password_hash)) {
            printf("Enter new password: ");
            scanf("%s", new_password);
            printf("Confirm new password: ");
            scanf("%s", confirm_password);
            
            if (strcmp(new_password, confirm_password) == 0) {
                int user_index = find_user(current_user.username);
                hash_password(new_password, users[user_index].password_hash);
                current_user = users[user_index];
                save_users();
                printf("Password updated successfully.\n");
            } else {
                printf("Passwords do not match.\n");
            }
        } else {
            printf("Invalid current password.\n");
        }
    }
}