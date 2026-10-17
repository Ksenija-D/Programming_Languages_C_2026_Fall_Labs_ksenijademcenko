// week5_task4_struct_binary_io.c   (BONUS)
// Name: Ksenija  Surname: Demchenko  Student ID: 251RDC083
//
// Task 4: Save and load a struct in binary format with fwrite()/fread()
// Week 5 – Files & Modular Programming
//
// Build: make lab5   Run: ./bin/week5_task4_struct_binary_io

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NAME_LEN 50

typedef struct Student {
  char name[MAX_NAME_LEN];
  int age;
  float gpa;
} Student;

// Function prototypes (do not change these signatures – the autograder calls
// them directly)
int save_student_binary(const Student* s,
                        const char* filename);  // 0 on success, -1 on error
int load_student_binary(const char* filename,
                        Student* out);  // 0 on success, -1 on error
long file_size(const char* filename);   // bytes, -1 on error

int main(void) {
  Student s1 = {
      0};  // zero-initialised so unused name bytes are not garbage on disk
  strcpy(s1.name, "Alice");
  s1.age = 21;
  s1.gpa = 3.75f;

  Student loaded = {0};
  const char* bin_file = "student.bin";
  const char* txt_file = "student.txt";

  printf("Saving student in binary format...\n");
  if (save_student_binary(&s1, bin_file) != 0) {
    printf("Error: could not save %s\n", bin_file);
    return EXIT_FAILURE;
  }

  FILE* fp = fopen(txt_file, "w");
  if (fp == NULL) {
    perror(txt_file);
    return EXIT_FAILURE;
  }
  fprintf(fp, "%s %d %.2f\n", s1.name, s1.age, s1.gpa);
  fclose(fp);

  printf("Loading student from binary file...\n");
  if (load_student_binary(bin_file, &loaded) != 0) {
    printf("Error: could not load %s\n", bin_file);
    return EXIT_FAILURE;
  }

  printf("Loaded student: %s, %d, GPA %.2f\n", loaded.name, loaded.age,
         loaded.gpa);
  printf("Text file size: %ld bytes\n", file_size(txt_file));
  printf("Binary file size: %ld bytes\n", file_size(bin_file));

  return EXIT_SUCCESS;
}

int save_student_binary(const Student* s, const char* filename) {
  FILE* fp = fopen(filename, "wb");
  if (fp == NULL) {
    perror(filename);
    return -1;
  }

  size_t written = fwrite(s, sizeof(Student), 1, fp);
  fclose(fp);
  return (written == 1) ? 0 : -1;
}

int load_student_binary(const char* filename, Student* out) {
  FILE* fp = fopen(filename, "rb");
  if (fp == NULL) {
    perror(filename);
    return -1;
  }

  size_t items = fread(out, sizeof(Student), 1, fp);
  fclose(fp);
  return (items == 1) ? 0 : -1;
}

long file_size(const char* filename) {
  FILE* fp = fopen(filename, "rb");
  if (fp == NULL) {
    perror(filename);
    return -1;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  long size = ftell(fp);
  fclose(fp);
  return size;
}
