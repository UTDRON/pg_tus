#ifndef UTILS_H
#define UTILS_H


// double calculate_mean(char **column_values, int num_values);
void testFunction(void);
int generateTrigrams(char *str, char trigrams[][4]);
int findTrigram(char trigrams[][4], int count, char *trigram);
double cosineSimilarityOfStrings(char *str1, char *str2);


#endif 
