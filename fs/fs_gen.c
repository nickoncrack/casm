#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DEFAULT_CLUSTER_SIZE 0x1000

#define FAT_EOF         0xFFFFFFFF
#define FAT_SB_MAGIC    "FAT PART"
#define FAT_CL_FREE     0x01
#define FAT_ENTRY_SIZE  sizeof(uint32_t)
#define FAT_DATA_SIZE   (s.cluster_size - sizeof(struct cluster))

#define FS_FILE         0x01
#define FS_DIR          0x02
#define FS_NODE_MAGIC   0xF0F0F0F0
#define FS_ROOT_CL_IDX  (fat_size / s.cluster_size + 2)

#ifdef _WIN32
#define strtok_r strtok_s
#endif

#define READ_CLUSTER(dst, i) \
    fseek(fp, i * s.cluster_size, SEEK_SET); \
    fread(dst, s.cluster_size, 1, fp)

#define WRITE_CLUSTER(src, i) \
    fseek(fp, i * s.cluster_size, SEEK_SET); \
    fwrite(src, s.cluster_size, 1, fp)


struct superblock {
    char magic[8];

    uint64_t size: 48;
    uint16_t cluster_size;

    char reserved[16];
} __attribute__((packed));

typedef struct {
    uint32_t magic;

    uint8_t flags;
    uint8_t mode;
    uint32_t size;
    uint32_t start_cluster;

    char name[32];
    char reserved[18];
} __attribute__((packed)) node_t;

struct cluster {
    uint8_t free : 1;
    uint8_t node : 1;

    uint8_t reserved : 6;
    uint8_t reserved1[3];
} __attribute__((packed));


struct superblock s;
uint32_t first_free_cluster;
uint32_t fat_size;
uint32_t *FAT;


char **split_string(const char *str, const char *delimiter, int *num_tokens) {
    char *str_copy, *token, *saveptr;
    int count = 0;
    char **result = NULL;
    str_copy = strdup(str);

    // count the tokens to allocate memory
    token = strtok_r(str_copy, delimiter, &saveptr);
    while (token != NULL) {
        count++;
        token = strtok_r(NULL, delimiter, &saveptr);
    }

    result = (char **) malloc(count * sizeof(char*));
    
    // extract the tokens
    strcpy(str_copy, str);
    token = strtok_r(str_copy, delimiter, &saveptr);
    count = 0;
    while (token != NULL) {
        result[count] = strdup(token);
        count++;
        token = strtok_r(NULL, delimiter, &saveptr);
    }

    free(str_copy);
    *num_tokens = count;
    return result;
}

void find_next_free_cluster(FILE *fp) {
    uint32_t i = first_free_cluster * s.cluster_size;
    struct cluster c;

    while (i < s.size) {
        fseek(fp, i, SEEK_SET);
        fread(&c, sizeof(struct cluster), 1, fp);
        if (c.free) {
            first_free_cluster = i / s.cluster_size;
            return;
        }

        i += s.cluster_size;
    }

    perror("Failed to find a free cluster");
    return;
}

void write_FAT(FILE *fp) {
    uint32_t *ptr = FAT;
    uint32_t nclusters = fat_size / FAT_DATA_SIZE; // full clusters
    uint32_t rem = fat_size % FAT_DATA_SIZE; // bytes to write in last cluster

    struct cluster *c = calloc(1, sizeof(struct cluster));

    // the FAT always starts at the second cluster
    fseek(fp, s.cluster_size, SEEK_SET);
    for (int i = 0; i < nclusters; i++) {
        fwrite(c, sizeof(struct cluster), 1, fp);
        fwrite(ptr, FAT_DATA_SIZE, 1, fp);

        ptr += FAT_DATA_SIZE;
    }

    // write last cluster
    fwrite(c, sizeof(struct cluster), 1, fp);
    fwrite(ptr, rem, 1, fp);

    free(c);
    return;
}

uint32_t find_node(FILE *fp, char *path, uint8_t type, uint8_t parent, node_t *dst) {
    int nsplits = 0;
    char **pwd_split = split_string(path, "/", &nsplits);
    if (parent) {
        nsplits--;
        type = FS_DIR;
    }

    uint8_t *cluster = calloc(1, s.cluster_size);
    struct cluster *c = (struct cluster *) cluster;
    node_t *node = (node_t *) (cluster + sizeof(struct cluster));

    // read root node
    READ_CLUSTER(cluster, FS_ROOT_CL_IDX);

    memcpy(node, cluster + sizeof(struct cluster), sizeof(node_t));
    if (parent && nsplits == 0) {
        memcpy(dst, node, sizeof(node_t));
        free(cluster);
        return 1;
    }

    uint32_t *children = (uint32_t *) (cluster + sizeof(node_t) + sizeof(struct cluster));

    uint8_t *temp_cluster = malloc(s.cluster_size);
    struct cluster *temp_c = (struct cluster *) temp_cluster;
    node_t *temp_node = (node_t *) (temp_cluster + sizeof(struct cluster));

    // iterate through all subdirectories in the path
    uint8_t found = 0;
    for (int i = 0; i < nsplits; i++) {
        if (i < nsplits-1) assert((node->flags & FS_DIR) != 0);

        // iterate through children
        found = 0;
        for (int j = 0; j < node->size; j++) {
            if (*children == 0) {
                free(temp_cluster);
                free(cluster);
                free(pwd_split);
                
                return 0;
            }

            READ_CLUSTER(temp_cluster, *children);
            assert(temp_c->node); // cluster has to contain a node

            uint8_t target_type = (i == nsplits-1) ? type : FS_DIR;
            if ((temp_node->flags & target_type) != 0) {
                if (strcmp(temp_node->name, pwd_split[i]) == 0) {
                    memcpy(cluster, temp_cluster, s.cluster_size);
                    children = (uint32_t *) (cluster + sizeof(node_t) + sizeof(struct cluster));

                    found = 1;
                    break;
                }
            }

            children++;
        }

        if (!found) {
            free(temp_cluster);
            free(cluster);
            free(pwd_split);
            
            return 0;
        }
    }

    memcpy(dst, node, sizeof(node_t));

    free(temp_cluster);
    free(cluster);
    for (int k = 0; k < nsplits; k++) {
        free(pwd_split[k]);
    }
    free(pwd_split);

    return 1;
}

node_t *mknode(FILE *fp, char *path, uint8_t type) {
    node_t *parent = malloc(sizeof(node_t));
    uint32_t ret = find_node(fp, path, FS_DIR, 1, parent);
    if (ret == 0) {
        return NULL;
    }

    assert((parent->flags & FS_DIR) != 0);

    find_next_free_cluster(fp);
    uint32_t start_cluster = first_free_cluster;

    // find last cluster of parent
    struct cluster sc_header;
    uint32_t i = parent->start_cluster;
    while (FAT[i] != FAT_EOF) {
        i = FAT[i];
        fseek(fp, i * s.cluster_size, SEEK_SET);
        fread(&sc_header, sizeof(struct cluster), 1, fp);
        assert(!(sc_header.free | sc_header.node)); // cluster cannot be free or contain a node
    } // i = last used cluster of parent

    // check if cluster is full
    int64_t temp = parent->size * FAT_ENTRY_SIZE - (FAT_DATA_SIZE - sizeof(node_t)); // size of parent minus size of first cluster 
    if (temp >= 0) {
        // if the first or the last cluster is full
        if (temp == 0 || FAT_DATA_SIZE - (temp % (FAT_DATA_SIZE)) < FAT_ENTRY_SIZE) {
            // expand the folder's linked list
            FAT[i] = first_free_cluster;
            FAT[first_free_cluster] = FAT_EOF;
            i = first_free_cluster;

            // find new cluster for the node
            find_next_free_cluster(fp);
            start_cluster = first_free_cluster;
        }
    }

    uint32_t *parent_cluster = malloc(s.cluster_size);
    READ_CLUSTER(parent_cluster, i);
    parent_cluster += (sizeof(struct cluster) + sizeof(node_t)) / FAT_ENTRY_SIZE;

    uint16_t j = 0;
    for (j = 0; j < FAT_DATA_SIZE / FAT_ENTRY_SIZE; j++) {
        if (parent_cluster[j] == 0) break;
    }
    assert(parent_cluster[j] == 0); // the loop might not have been broken
    
    // write child cluster index into parent
    fseek(fp, i * s.cluster_size + j * FAT_ENTRY_SIZE + sizeof(struct cluster) + sizeof(node_t), SEEK_SET);
    fwrite(&start_cluster, FAT_ENTRY_SIZE, 1, fp);

    // update parent size
    parent->size++;
    fseek(fp, parent->start_cluster * s.cluster_size + sizeof(struct cluster), SEEK_SET);
    fwrite(parent, sizeof(node_t), 1, fp);

    int nsplits = 0;
    char **pwd_split = split_string(path, "/", &nsplits);

    FAT[start_cluster] = FAT_EOF;
    node_t *child = calloc(1, sizeof(node_t));
    child->magic = FS_NODE_MAGIC;
    child->start_cluster = start_cluster;
    child->flags |= type;
    child->size = 0;
    child->mode = 0;
    strcpy(child->name, pwd_split[nsplits-1]);

    for (int k = 0; k < nsplits; k++) {
        free(pwd_split[k]);
    }
    free(pwd_split);
    free(parent_cluster);
    free(parent);

    memset(&sc_header, 0, sizeof(struct cluster));
    sc_header.node = 1;

    void *cluster = calloc(1, s.cluster_size);
    memcpy(cluster, &sc_header, sizeof(struct cluster));
    memcpy(cluster+sizeof(struct cluster), child, sizeof(node_t));
    WRITE_CLUSTER(cluster, start_cluster);

    free(cluster);

    write_FAT(fp);
    return child;
}

// args: output file, fs size (in clusters), input folder
int main(int argc, const char *argv[]) {
    if (argc != 4) {
        return -1;
    }

    FILE *fp = fopen(argv[1], "wb+");
    if (!fp) {
        perror("Failed to open output file");
        return -1;
    }

    uint32_t size = atoi(argv[2]);
    uint8_t *buff = calloc(1, DEFAULT_CLUSTER_SIZE);

    s.size = size * DEFAULT_CLUSTER_SIZE;
    s.cluster_size = DEFAULT_CLUSTER_SIZE;
    memcpy(s.magic, FAT_SB_MAGIC, 8);

    memcpy(buff, &s, sizeof(struct superblock));
    fwrite(buff, DEFAULT_CLUSTER_SIZE, 1, fp);

    memset(buff, 0, DEFAULT_CLUSTER_SIZE);

    fat_size = size * FAT_ENTRY_SIZE;
    FAT = calloc(1, fat_size);
    first_free_cluster = fat_size / DEFAULT_CLUSTER_SIZE + ((fat_size % DEFAULT_CLUSTER_SIZE) != 0) + 1;

    struct cluster *c = calloc(1, sizeof(struct cluster));
    c->free = FAT_CL_FREE;
    for (uint32_t i = 1; i <= first_free_cluster; i += 1) {
        fwrite(buff, DEFAULT_CLUSTER_SIZE, 1, fp);
    }

    memcpy(buff, c, sizeof(struct cluster));
    for (uint32_t i = first_free_cluster+1; i < size; i+= 1) {
        fwrite(buff, DEFAULT_CLUSTER_SIZE, 1, fp);
    }

    // write root directory
    memset(buff, 0, DEFAULT_CLUSTER_SIZE);
    node_t *root = calloc(1, sizeof(node_t));
    root->magic = FS_NODE_MAGIC;
    root->flags = FS_DIR;
    root->start_cluster = FS_ROOT_CL_IDX;
    strcpy(root->name, "ROOT");

    c->free = 0;
    c->node = 1;
    FAT[FS_ROOT_CL_IDX] = FAT_EOF;

    write_FAT(fp);
    fseek(fp, FS_ROOT_CL_IDX * s.cluster_size, SEEK_SET);
    fwrite(c, sizeof(struct cluster), 1, fp);
    fwrite(root, sizeof(node_t), 1, fp);
    
    free(root);
    free(buff);

    // test entries
    mknode(fp, "/dir", FS_DIR);
    mknode(fp, "/dir/dir", FS_DIR);
    mknode(fp, "/dir/dir/file", FS_FILE);

    node_t *n = malloc(sizeof(node_t));
    uint8_t r = find_node(fp, "/dir/dir/file", FS_FILE, 0, n);
    if (r != 0) {
        printf("node->name: %s\n", n->name);
    }

    fclose(fp);

    free(n);
    free(c);
    free(FAT);
    return 0;
}
