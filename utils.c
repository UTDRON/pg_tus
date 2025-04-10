#include "utils.h"

#include "postgres.h"
#include "fmgr.h"

#include "utils/builtins.h"
#include "utils/array.h"
#include "utils/elog.h"
#include "utils/geo_decls.h"
#include "catalog/pg_type.h"

#include "executor/spi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libpq-fe.h>

#define MAX_TRIGRAMS 1000


void testFunction()
{
    elog(INFO, "YOUR CALL SUCCEEDED");
}

int generateTrigrams(char *str, char trigrams[][4]) {
    int len = strlen(str);
    int count = 0;
    for (int i = 0; i < len - 2; i++) {
        snprintf(trigrams[count], 4, "%c%c%c", str[i], str[i+1], str[i+2]);
        count++;
    }
    return count;
}

/* finding the index of a trigram in a list of unique trigrams */ 
int findTrigram(char trigrams[][4], int count, char *trigram) {
    for (int i = 0; i < count; i++) {
        if (strcmp(trigrams[i], trigram) == 0) {
            return i;
        }
    }
    return -1;
}

double cosineSimilarityOfStrings(char *str1, char *str2) {
    char trigrams1[MAX_TRIGRAMS][4], trigrams2[MAX_TRIGRAMS][4];
    int count1 = generateTrigrams(str1, trigrams1);
    int count2 = generateTrigrams(str2, trigrams2);

    char unique_trigrams[MAX_TRIGRAMS][4];
    int unique_count = 0;

    /* collecting unique trigrams from both strings */
    for (int i = 0; i < count1; i++) {
        if (findTrigram(unique_trigrams, unique_count, trigrams1[i]) == -1) {
            strcpy(unique_trigrams[unique_count++], trigrams1[i]);
        }
    }
    
    for (int i = 0; i < count2; i++) {
        if (findTrigram(unique_trigrams, unique_count, trigrams2[i]) == -1) {
            strcpy(unique_trigrams[unique_count++], trigrams2[i]);
        }
    }

    /* counts of each unique trigram in each string */
    int vector1[MAX_TRIGRAMS] = {0};
    int vector2[MAX_TRIGRAMS] = {0};

    /* counts of each trigram from str1 */
    for (int i = 0; i < count1; i++) {
        int index = findTrigram(unique_trigrams, unique_count, trigrams1[i]);
        if (index != -1) {
            vector1[index]++;
        }
    }

    /* counts of each trigram from str2 */
    for (int i = 0; i < count2; i++) {
        int index = findTrigram(unique_trigrams, unique_count, trigrams2[i]);
        if (index != -1) {
            vector2[index]++;
        }
    }

    double dot_product = 0.0, magnitude1 = 0.0, magnitude2 = 0.0;
    for (int i = 0; i < unique_count; i++) {
        dot_product += vector1[i] * vector2[i];
        magnitude1 += vector1[i] * vector1[i];
        magnitude2 += vector2[i] * vector2[i];
    }

    if (magnitude1 == 0 || magnitude2 == 0) {
        return 0.0;
    }

    return dot_product / (sqrt(magnitude1) * sqrt(magnitude2));
}