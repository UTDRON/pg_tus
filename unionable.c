#include "postgres.h"
#include "fmgr.h"
#include "catalog/pg_type.h"

#include "executor/spi.h"
#include <libpq-fe.h>

#include "utils/builtins.h"
#include "utils/array.h"
#include "utils/elog.h"
#include "utils/geo_decls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdbool.h>

// #include <limits.h>
// #include <gsl/gsl_statistics.h>
#include "utils.h"


struct Encoding processColumn(char **column_values, char * data_type, int num_rows, char * column_name, char * table_name);
void normalizeVector(double vector[4]);
void addEncoding(struct Encoding new_encoding);
void executeQueries(char * query_table_name);
void calculateSimilarities(void);
double cosineSimilarity(const double *array1, const double *array2, size_t length);
int compareValues(const void *a, const void *b);
int compareSimilarity(const void *a, const void *b);
int compareMatchScore(const void *a, const void *b);
double calculatePercentile(double *data, int size, double percentile);
// double findGreedyMatch(struct Similarities *sorted_similarities, int size);
struct NumericSummaryStats calculateNumericSummaryStats(char **column_values, int num_values);
struct StringSummaryStats calculateStringSummaryStats(char **column_values, int num_values);


struct Encoding {
    char * table_name;
    char * column_name;
    char * data_type;
    double vector[9];       
};

struct ColumnNode {
    char * table_name;
    char * attr_name;
    bool done;
};

struct Similarities {
    struct ColumnNode *query_ColumnNode;
    struct ColumnNode *candidate_ColumnNode;
    double similarity_score;
    // double name_sim;
};


struct TableRanks {
    char * table_name;
    double match_score
};

struct NumericSummaryStats{
    double count;
    double mean;
    double stddev;
    double min;
    double percentile_25;
    double median;  
    double percentile_75;
    double max;
    double range;
};

struct StringSummaryStats{
    double count;
    double mean;
    double stddev;
    int min;
    double average_numerical_chars_ratio;
    double average_whitespace_ratio;
    int max;
    double range;
};


struct Encoding* candidate_encodings_array          = NULL;
struct Encoding* query_encodings_array              = NULL;
struct Similarities * similarity_tuples             = NULL;
struct TableRanks * table_ranks                     = NULL;

size_t num_query_attrs                              = 0;    
size_t capacity                                     = 0;              
size_t num_candidate_attrs                          = 0; 
size_t num_similarity_tuples                        = 0;

int *num_columns_array                              = NULL;
int size_of_num_columns_array                       = 0;


double cosineSimilarity(const double *array1, const double *array2, size_t length) {
    /*
    The arrays passed are already normalized, so only dot product is sufficient
    */
    double dot_product = 0.0;

    for (size_t i = 0; i < length; i++) {
        dot_product += array1[i] * array2[i];
    }

    return dot_product;
}


void normalizeVector(double vector[9]) {
    double magnitude = 0.0;
    for (int i = 0; i < 9; i++) {
        magnitude += vector[i] * vector[i];
    }
    magnitude = sqrt(magnitude);

    if (magnitude == 0.0) {
        elog(ERROR, "Cannot normalize a zero vector.");
        return;
    }

    for (int i = 0; i < 9; i++) {
        vector[i] /= magnitude;
    }
}

struct Encoding processColumn(char **column_values, char * data_type, int num_rows, char * column_name, char * table_name) {

    struct Encoding column;
    if (strcmp(data_type, "varchar") == 0)
    {
        struct StringSummaryStats stats     = calculateStringSummaryStats(column_values, num_rows);
        column.table_name                   = table_name;
        column.column_name                  = column_name;
        column.data_type                    = "text";
        column.vector[0]                    = stats.count;             // count
        column.vector[1]                    = stats.mean;              // mean
        column.vector[2]                    = stats.stddev;            // stddev
        column.vector[3]                    = stats.min;               // min
        column.vector[4]                    = stats.average_numerical_chars_ratio; // average_numerical_chars_ratio
        column.vector[5]                    = stats.average_whitespace_ratio;      // average_whitespace_ratio
        column.vector[6]                    = stats.max;               // max
        column.vector[7]                    = stats.range;             // range
        column.vector[8]                    = stats.range;             // range
        normalizeVector(column.vector);
    }
    else if ((strcmp(data_type, "numeric") == 0) || (strstr(data_type, "int")))
    {
        struct NumericSummaryStats stats   = calculateNumericSummaryStats(column_values, num_rows);
        column.table_name           = table_name;
        column.column_name          = column_name;
        column.data_type            = "numeric";
        column.vector[0]            = stats.count;             // count
        column.vector[1]            = stats.mean;              // mean
        column.vector[2]            = stats.stddev * stats.stddev ;  // stddev
        column.vector[3]            = stats.min;               // min
        column.vector[4]            = stats.percentile_25;     // percentile25
        column.vector[5]            = stats.median;            // median
        column.vector[6]            = stats.percentile_75;     // percentile_75
        column.vector[7]            = stats.max;               // max
        column.vector[8]            = stats.range;             // range
        normalizeVector(column.vector);
    }
    else 
    {
        // elog(INFO, "UNKNOWN TYPE: %s in table: %s as column: %s", data_type, table_name, column_name); // UNCOMMENT
        column.table_name           = table_name;
        column.column_name          = column_name;
        column.data_type            = "unknown";
        column.vector[0]            = num_rows;         // count
        column.vector[1]            = 10.0;        // mean
        column.vector[2]            = 0.0;         // stddev
        column.vector[3]            = 10.0;         // min
        column.vector[4]            = 10.0;         // percentile25
        column.vector[5]            = 10.0;        // median
        column.vector[6]            = 10.0;        // percentile_75
        column.vector[7]            = 10.0;        // max
        column.vector[8]            = 0.0;        // range
        normalizeVector(column.vector);
    }
    return column;
}

void addEncoding(struct Encoding new_encoding) {
    
    if (num_candidate_attrs == capacity) {
        capacity = capacity == 0 ? 4 : capacity * 2; 
        struct Encoding* temp               = realloc(candidate_encodings_array, capacity * sizeof(struct Encoding));
        if (!temp) {

            elog(ERROR, "Memory allocation failed");
            free(candidate_encodings_array);
            exit(1);
        }
        candidate_encodings_array = temp;
    }

    candidate_encodings_array[num_candidate_attrs++] = new_encoding;
}


void executeQueries(char * query_table_name) {

    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "Could not connect to SPI");
        return;
    }

    char *table_query                       = pstrdup("SELECT tablename FROM pg_tables WHERE schemaname = 'public';");
    int ret                                 = SPI_execute(table_query, true, 0);

    if (ret != SPI_OK_SELECT) {
        elog(ERROR, "Failed to fetch table names");
        SPI_finish();
        return;
    }

    SPITupleTable *tuptable                 = SPI_tuptable;
    TupleDesc tupdesc                       = tuptable->tupdesc;
    uint64 num_tables                       = tuptable->numvals;

    size_of_num_columns_array               = num_tables;
    num_columns_array                       = (int *)malloc(num_tables * sizeof(int));
    

    for (uint64 j = 0; j < num_tables; j++) {
        HeapTuple table_tuple               = tuptable->vals[j];

        char *table_name                    = SPI_getvalue(table_tuple, tupdesc, 1);
        if (!table_name) continue;

        // elog(INFO, "Table: %s", table_name);

        char data_query[1024];
        snprintf(data_query, sizeof(data_query), "SELECT * FROM %s;", table_name);

        int ret_data                        = SPI_execute(data_query, true, 0);
        if (ret_data != SPI_OK_SELECT) {
            elog(WARNING, "Could not fetch data from table %s", table_name);
            continue;
        }

        SPITupleTable *data_tuptable        = SPI_tuptable;
        TupleDesc data_tupdesc              = data_tuptable->tupdesc;

        int num_columns                     = data_tupdesc->natts;
        uint64 num_rows                     = data_tuptable->numvals;
        char buf[8192];
        // char * data_type;
        HeapTuple row;
        char **column_values;
        char column_names[8192];
        column_names[0]                     = '\0';
        

        if (strcmp(table_name, query_table_name) == 0)
        {
            query_encodings_array = realloc(query_encodings_array, sizeof(struct Encoding) * num_columns);
            if (query_encodings_array == NULL) {
                elog(ERROR, "Memory allocation failed");
                free(query_encodings_array);
                exit(1);
            }
            num_query_attrs = (size_t) num_columns;
            num_columns_array[j]            = 0;
        }
        else
        {
            num_columns_array[j]            = num_columns;
        }

        for (int i = 1; i <= num_columns; i++) {
            snprintf(column_names + strlen(column_names), sizeof(column_names) - strlen(column_names), " %s%s",
                     SPI_fname(data_tupdesc, i),
                     (i == num_columns) ? " " : " |");
            
            buf[0]                          = '\0';  // Reset buffer for each row
            // elog(INFO, "Column: %s", SPI_fname(data_tupdesc, i));
            column_values                   = (char **)palloc(num_rows * sizeof(char *));

            for (uint64 k = 0; k < num_rows; k++) {
                row                         = data_tuptable->vals[k];
                char *value                 = SPI_getvalue(row, data_tupdesc, i);
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %s%s", value, (i == num_columns) ? " " : " |");
                if (value == NULL)
                {
                    column_values[k]        = pstrdup("NULL");
                }
                else
                {
                    column_values[k]        = pstrdup(value);
                }
            }
            // elog(INFO, "Data: %s", buf);
            
            struct Encoding column          = processColumn(column_values, SPI_gettype(data_tupdesc, i), num_rows, SPI_fname(data_tupdesc, i), table_name);
            
            if (strcmp(table_name, query_table_name) == 0){
                query_encodings_array[i-1]  = column;
            }
            else
            {
                addEncoding(column);
            }
        }

        // elog(INFO, "Columns: %s", column_names);
        
        pfree(column_values);
        SPI_freetuptable(data_tuptable);  
    }

    for (size_t i = 0; i < num_tables; i ++)
    {
        if (i != 0)
        {
            num_columns_array[i] = num_columns_array[i - 1] + num_columns_array[i]; 
        }
        
    }
    

    // elog(INFO, "ENCODINGS FOR QUERY TABLE"); // UNCOMMENT
    // for (size_t i = 0; i < num_query_attrs; i++) {
    //     elog(INFO, "Encoding %zu: table_name=%s, column_name=%s, data_type=%s, vector=[%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f]",
    //            i, query_encodings_array[i].table_name, query_encodings_array[i].column_name, query_encodings_array[i].data_type,
    //            query_encodings_array[i].vector[0], query_encodings_array[i].vector[1], query_encodings_array[i].vector[2], query_encodings_array[i].vector[3],
    //            query_encodings_array[i].vector[4], query_encodings_array[i].vector[5], query_encodings_array[i].vector[6], query_encodings_array[i].vector[7],
    //            query_encodings_array[i].vector[8], query_encodings_array[i].vector[9]);
    // }

    // elog(INFO, "ENCODINGS FOR CANDIDATE TABLES"); // UNCOMMENT
    // for (size_t i = 0; i < num_candidate_attrs; i++) {
    //     elog(INFO, "Encoding %zu: table_name=%s, column_name=%s, data_type=%s, vector=[%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f]",
    //            i, candidate_encodings_array[i].table_name, candidate_encodings_array[i].column_name, candidate_encodings_array[i].data_type,
    //            candidate_encodings_array[i].vector[0], candidate_encodings_array[i].vector[1], candidate_encodings_array[i].vector[2], candidate_encodings_array[i].vector[3],
    //            candidate_encodings_array[i].vector[4], candidate_encodings_array[i].vector[5], candidate_encodings_array[i].vector[6], candidate_encodings_array[i].vector[7],
    //            candidate_encodings_array[i].vector[8], candidate_encodings_array[i].vector[9]);
    // }

    SPI_finish();  
    pfree(table_query);  
}


PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(unionableFindTopK);
Datum
unionableFindTopK(PG_FUNCTION_ARGS)
{
    char *query_table_name                          = text_to_cstring(PG_GETARG_TEXT_PP(0));
    int top_k                                       = PG_GETARG_INT32(1);

    if (top_k == 0)
    {
        PG_RETURN_TEXT_P(cstring_to_text("NOTHING TO RETURN!!!"));
    }

    // testFunction();

    executeQueries(query_table_name);

    pfree(query_table_name);

    calculateSimilarities();

    qsort(table_ranks, size_of_num_columns_array, sizeof(struct TableRanks), compareMatchScore);

    StringInfoData result;
    initStringInfo(&result);

    elog(INFO, "_____________RANKED TABLES__________________");
    for (size_t k = 0; k < size_of_num_columns_array; k++)
    {
        elog(INFO, "( %s, %f )", table_ranks[k].table_name, table_ranks[k].match_score);
    }

    for (size_t i = 0; i < top_k && i < size_of_num_columns_array - 1; i++) {
        if (i > 0) {
            appendStringInfo(&result, "\n "); 
        }
        appendStringInfo(&result, "%s", table_ranks[i].table_name); 
    }

    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}


PG_FUNCTION_INFO_V1(create_encoding);
Datum
create_encoding(PG_FUNCTION_ARGS)
{
    char *table_create_query                        = pstrdup("CREATE TABLE if not exists encodings (tbl_name VARCHAR, column_name VARCHAR, vector DOUBLE PRECISION[]);");
    
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "Could not connect to SPI");
        PG_RETURN_TEXT_P(cstring_to_text("NOT DONE!!!"));
    }

    int ret                                         = SPI_execute(table_create_query, true, 0);
    if (ret != SPI_OK_UTILITY) {
        elog(ERROR, "Failed to create encoding table");
        SPI_finish();
        pfree(table_create_query);
        PG_RETURN_TEXT_P(cstring_to_text("NOT DONE!!!"));
    }

    SPI_finish();  
    pfree(table_create_query);

    PG_RETURN_TEXT_P(cstring_to_text("DONE!!!"));
}


int compareValues(const void *a, const void *b) {
    double diff                 = *(double*)a - *(double*)b;
    return (diff > 0) - (diff < 0);
}

int compareSimilarity(const void *a, const void *b) {
    double scoreA = ((struct Similarities *)a)->similarity_score;
    double scoreB = ((struct Similarities *)b)->similarity_score;
    if (scoreA < scoreB) return 1;
    else if (scoreA > scoreB) return -1;
    return 0;
}

int compareMatchScore(const void *a, const void *b) {
    struct TableRanks *rankA = (struct TableRanks *)a;
    struct TableRanks *rankB = (struct TableRanks *)b;

    if (rankA->match_score < rankB->match_score) {
        return 1;  
    } else if (rankA->match_score > rankB->match_score) {
        return -1; 
    } else {
        return 0; 
    }
}

double calculatePercentile(double *data, int size, double percentile) {
    double index                = (percentile / 100.0) * (size - 1);
    int lower                   = (int)index;
    int upper                   = lower + 1;
    double weight               = index - lower;
    if (upper >= size) return data[lower];
    return data[lower] * (1 - weight) + data[upper] * weight;
}

struct NumericSummaryStats calculateNumericSummaryStats(char **column_values, int num_values) {
    struct NumericSummaryStats stats;

    /*count*/
    stats.count             = num_values;
    stats.mean              = 0.0;
    stats.stddev            = 0.0;
    stats.min               = 0.0;
    stats.percentile_25     = 0.0;
    stats.median            = 0.0;
    stats.percentile_75     = 0.0;
    stats.max               = 0.0;
    stats.range             = 0.0;

    if (num_values == 0) {
        elog(ERROR, "No values to calculate statistics.");
        return stats;
    }

    double *values = malloc(num_values * sizeof(double));

    if (values == NULL) {
        elog(INFO, "Memory allocation failed.");
        return stats;
    }

    double sum = 0.0;
    stats.min                   = INFINITY;
    stats.max                   = -INFINITY;

    for (int i = 0; i < num_values; i++) {
        if (column_values[i] != NULL) {
            values[i]           = atof(column_values[i]);
        }
        else 
        {
            values[i]           = 0.0;
        }
        sum += values[i];
        /* Calculated min and max values */
        if (values[i] < stats.min) stats.min = values[i];
        if (values[i] > stats.max) stats.max = values[i];
    }

    /* Calculated mean */ 
    stats.mean                  = sum / stats.count;

    /* Calculated stddev */ 
    double sum_squared_diff = 0.0;
    for (int i = 0; i < num_values; i++) {
        sum_squared_diff        += pow(values[i] - stats.mean, 2);
    }
    stats.stddev                = sqrt(sum_squared_diff / num_values);

    /* Sort values for percentile and median calculation */ 
    qsort(values, num_values, sizeof(double), compareValues);

    /* Calculate percentiles and median */
    stats.percentile_25         = calculatePercentile(values, num_values, 25.0);
    stats.median                = calculatePercentile(values, num_values, 50.0);
    stats.percentile_75         = calculatePercentile(values, num_values, 75.0);

    /* range */
    stats.range                 = stats.max - stats.min;

    // Free dynamically allocated memory
    free(values);

    return stats;
}


struct StringSummaryStats calculateStringSummaryStats(char **column_values, int num_values) {
    struct StringSummaryStats stats;
    
    stats.count                             = num_values;
    stats.mean                              = 0.0;
    stats.stddev                            = 0.0;
    stats.min                               = 0.0;
    stats.average_numerical_chars_ratio     = 0.0;
    stats.average_whitespace_ratio          = 0.0;
    stats.max                               = 0.0;
    stats.range                             = 0.0;

    if (num_values == 0) {
        elog(ERROR, "No values to calculate statistics.");
        return stats;
    }
    
    double total_numerical_ratio            = 0.0;
    double total_whitespace_ratio           = 0.0;
    int total_length                        = 0;
    double length_sum                       = 0.0;
    double length_sum_squared               = 0.0;

    stats.min                               = INFINITY;
    stats.max                               = -INFINITY;

    // Iterate through each string in the array
    for (int i = 0; i < num_values; i++) {
        char *str = column_values[i];
        
        if (str == NULL) {
            continue; // Skip NULL strings
        }

        int num_numerical_chars = 0;
        int num_whitespace_chars = 0;
        int total_chars = 0;

        // Count characters in the current string
        for (int j = 0; str[j] != '\0'; j++) {
            total_chars++;
            if (isdigit((unsigned char)str[j])) {
                num_numerical_chars++;
            } else if (isspace((unsigned char)str[j])) {
                num_whitespace_chars++;
            }
        }

        // Update min and max lengths
        if (total_chars < stats.min) stats.min = total_chars;
        if (total_chars > stats.max) stats.max = total_chars;

        // Accumulate length statistics
        length_sum += total_chars;
        length_sum_squared += total_chars * total_chars;

        // Calculate ratios for this string
        if (total_chars > 0) { // Avoid division by zero for empty strings
            double numerical_ratio = (double)num_numerical_chars / total_chars;
            double whitespace_ratio = (double)num_whitespace_chars / total_chars;

            // Accumulate the ratios
            total_numerical_ratio += numerical_ratio;
            total_whitespace_ratio += whitespace_ratio;
        }
    }

    // Calculate average ratios
    stats.average_numerical_chars_ratio = total_numerical_ratio / num_values;
    stats.average_whitespace_ratio = total_whitespace_ratio / num_values;

    // Calculate average length
    stats.mean = length_sum / num_values;

    // Calculate variance of lengths
    stats.stddev = (length_sum_squared / num_values) - (stats.mean * stats.mean);
    stats.range  = stats.max - stats.min;

    return stats;
}

double findGreedyMatch(struct Similarities *sorted_similarities, int size) {
    double match_score = 0.0;
    // elog(INFO, "_____________CURRENT RUNNING LIST(SORTED)__________________"); //UNCOMMENT
    // for (size_t idx= 0; idx < size; idx ++)
    // {
    //     elog(INFO, "%s.%s, %s.%s, %f\n",
    //             sorted_similarities[idx].query_ColumnNode->table_name,
    //             sorted_similarities[idx].query_ColumnNode->attr_name,
    //             sorted_similarities[idx].candidate_ColumnNode->table_name,
    //             sorted_similarities[idx].candidate_ColumnNode->attr_name,
    //             sorted_similarities[idx].similarity_score);
    // }

    // elog(INFO, "_____________MATCHES__________________"); //UNCOMMENT
    for (int i = 0; i < size; i++) {
        if (!sorted_similarities[i].query_ColumnNode->done && !sorted_similarities[i].candidate_ColumnNode->done) {
            // elog(INFO, "%s.%s matched with %s.%s (Similarity Score: %f)\n",
            //        sorted_similarities[i].query_ColumnNode->table_name, sorted_similarities[i].query_ColumnNode->attr_name,
            //        sorted_similarities[i].candidate_ColumnNode->table_name, sorted_similarities[i].candidate_ColumnNode->attr_name,
            //        sorted_similarities[i].similarity_score); //UNCOMMENT

            match_score += sorted_similarities[i].similarity_score;

            // Mark both nodes as matched
            sorted_similarities[i].query_ColumnNode->done = true;
            sorted_similarities[i].candidate_ColumnNode->done = true;

        }
    }
    return match_score;
}

void calculateSimilarities()
{
    struct ColumnNode *array_source_nodes = (struct ColumnNode *)malloc(num_query_attrs * sizeof(struct ColumnNode));
    struct ColumnNode *array_destination_nodes = (struct ColumnNode *)malloc(num_candidate_attrs * sizeof(struct ColumnNode));

    table_ranks = (struct TableRanks *)realloc(table_ranks, (size_of_num_columns_array) * sizeof(struct TableRanks));


    if (!array_source_nodes || !array_destination_nodes) {
        elog(ERROR, "Memory allocation failed for source and destination nodes array\n");
        exit(1);
    }

    for (size_t i = 0; i < num_query_attrs; i++) {
        array_source_nodes[i].table_name = query_encodings_array[i].table_name;
        array_source_nodes[i].attr_name = query_encodings_array[i].column_name;
        array_source_nodes[i].done = false;
    }

    for (size_t i = 0; i < num_candidate_attrs; i++) {
        array_destination_nodes[i].table_name = candidate_encodings_array[i].table_name;
        array_destination_nodes[i].attr_name = candidate_encodings_array[i].column_name;
        array_destination_nodes[i].done = false;
    }


    for (size_t k = 0; k < size_of_num_columns_array; k++)
    {
        struct Similarities *running_search_space = NULL;
        int counter = 0;
        for(size_t i = 0; i < num_query_attrs; i++)
        {
            size_t start_idx = (k == 0) ? 0 : num_columns_array[k - 1];
            
            for(size_t j = start_idx; j < num_columns_array[k]; j++) 
            {
                if (strcmp(query_encodings_array[i].data_type, candidate_encodings_array[j].data_type) == 0)
                {
                    running_search_space = (struct Similarities *)realloc(running_search_space, (counter + 1) * sizeof(struct Similarities));
                    if (!running_search_space) {
                        elog(ERROR, "Memory allocation failed for running search space\n");
                        exit(1);
                    }
                    running_search_space[counter].query_ColumnNode = &array_source_nodes[i];
                    running_search_space[counter].candidate_ColumnNode = &array_destination_nodes[j];
                    running_search_space[counter].similarity_score = cosineSimilarity(query_encodings_array[i].vector, candidate_encodings_array[j].vector, 9);
                    counter++;
                    
                }
            }
        }

        qsort(running_search_space, counter, sizeof(struct Similarities), compareSimilarity);

        if (counter > 0)
        {
            table_ranks[k].table_name = running_search_space[0].candidate_ColumnNode->table_name;
            table_ranks[k].match_score  = findGreedyMatch(running_search_space, counter);
        }
        else
        {
            table_ranks[k].table_name = NULL;
            table_ranks[k].match_score  = -INFINITY;
        }
        

        for (size_t idx= 0; idx < counter; idx ++)
        {
            running_search_space[idx].query_ColumnNode-> done = false;
            running_search_space[idx].candidate_ColumnNode-> done = false;
        }

        free(running_search_space);
        running_search_space = NULL;
    }

    // elog(INFO, "_____________UNRANKED TABLES__________________"); // UNCOMMENT
    // for (size_t k = 0; k < size_of_num_columns_array; k++)
    // {
    //     elog(INFO, "( %s, %f )", table_ranks[k].table_name, table_ranks[k].match_score);
    // }

    free(candidate_encodings_array);
    candidate_encodings_array = NULL;

    free(query_encodings_array);
    query_encodings_array = NULL;

}


