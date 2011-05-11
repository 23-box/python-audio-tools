#include "huffman.h"
#include <stdlib.h>
#include <string.h>


/********************************************************
 Audio Tools, a module and set of tools for manipulating audio data
 Copyright (C) 2007-2011  Brian Langenberger

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*******************************************************/

int bank_to_int(struct byte_bank bank) {
    if (bank.size > 0) {
        return (1 << bank.size) | bank.value;
    } else
        return 0;
}

struct huffman_node* build_huffman_tree(struct huffman_frequency* frequencies)
{
    unsigned int counter = 0;

    return build_huffman_tree_(0, 0, frequencies, &counter);
}

struct huffman_node* build_huffman_tree_(unsigned int bits,
                                         unsigned int length,
                                         struct huffman_frequency* frequencies,
                                         unsigned int* counter)
{
    int i;
    struct huffman_node* node = malloc(sizeof(struct huffman_node));

    /*go through the list of frequency values*/
    for (i = 0; frequencies[i].length != 0; i++) {
        /*if our bits and length value is found,
          generate a new leaf node from that frequency*/
        if ((frequencies[i].bits == bits) &&
            (frequencies[i].length == length)) {
            node->type = NODE_LEAF;
            node->v.leaf = frequencies[i].value;
            return node;
        }
    }

    /*otherwise, generate a new tree node
      whose leaf nodes are generated recursively*/
    node->type = NODE_TREE;
    node->v.tree.id = *counter;
    (*counter) += 1;
    node->v.tree.bit_0 = build_huffman_tree_(bits << 1,
                                             length + 1,
                                             frequencies,
                                             counter);
    node->v.tree.bit_1 = build_huffman_tree_((bits << 1) | 1,
                                             length + 1,
                                             frequencies,
                                             counter);
    return node;
}

void free_huffman_tree(struct huffman_node* node) {
    if (node->type == NODE_LEAF) {
        free(node);
    } else {
        free_huffman_tree(node->v.tree.bit_0);
        free_huffman_tree(node->v.tree.bit_1);
        free(node);
    }
}

void print_huffman_tree(struct huffman_node* node, int indent) {
    int i;
    for (i = 0; i < indent; i++) {
        printf(" ");
    }
    switch (node->type) {
    case NODE_LEAF:
        printf("leaf : %d\n", node->v.leaf);
        break;
    case NODE_TREE:
        printf("node (%u)\n", node->v.tree.id);
        print_huffman_tree(node->v.tree.bit_0, indent + 2);
        print_huffman_tree(node->v.tree.bit_1, indent + 2);
    }
}

int compile_huffman_tree(struct bs_huffman_table (**table)[][0x200],
                          struct huffman_node* tree,
                          bs_endianness endianness) {
    int total_rows = total_non_leaf_nodes(tree);

    /*populate the jump tables of each non-leaf node*/
    populate_huffman_tree(tree, endianness);

    /*allocate space for the entire set of jump tables*/
    *table = malloc(sizeof(struct bs_huffman_table) * total_rows * 0x200);

    /*transfer jump tables of each node from tree*/
    transfer_huffman_tree(*table, tree);

    return total_rows;
}

void populate_huffman_tree(struct huffman_node* tree,
                           bs_endianness endianness) {
    unsigned int size;
    unsigned int value;
    struct byte_bank bank;

    if (tree->type == NODE_TREE) {
        tree->v.tree.jump_table[0].context_node = 0;
        tree->v.tree.jump_table[0].value = 0;
        tree->v.tree.jump_table[1].context_node = 0;
        tree->v.tree.jump_table[1].value = 0;

        for (size = 1; size < (8 + 1); size++)
            for (value = 0; value < (1 << size); value++) {
                bank.size = size;
                bank.value = value;

                next_read_huffman_state(
                    &(tree->v.tree.jump_table[bank_to_int(bank)]),
                    bank, tree, endianness);
        }

        populate_huffman_tree(tree->v.tree.bit_0, endianness);
        populate_huffman_tree(tree->v.tree.bit_1, endianness);
    }
}

void next_read_huffman_state(struct bs_huffman_table* state,
                             struct byte_bank bank,
                             struct huffman_node* tree,
                             bs_endianness endianness) {
    struct byte_bank next_bank;

    if (tree->type == NODE_LEAF) {
        /*reached a leaf node, so return byte bank and value*/
        state->context_node = bank_to_int(bank);
        state->value = tree->v.leaf;
    } else if (bank.size == 0) {
        /*exhausted byte bank, so return empty bank and current node*/
        state->context_node = tree->v.tree.id << 9;
        state->value = 0;
    } else if (endianness == BS_LITTLE_ENDIAN) {
        /*progress through bit stream in little endian order*/
        next_bank = bank;
        next_bank.value >>= 1;
        next_bank.size -= 1;

        if (bank.value & 1) {
            next_read_huffman_state(state,
                                    next_bank,
                                    tree->v.tree.bit_1,
                                    endianness);
        } else {
            next_read_huffman_state(state,
                                    next_bank,
                                    tree->v.tree.bit_0,
                                    endianness);

        }
    } else if (endianness == BS_BIG_ENDIAN) {
        /*progress through bit stream in big endian order*/
        next_bank = bank;
        next_bank.size -= 1;

        if (bank.value & (1 << (bank.size - 1))) {
            next_read_huffman_state(state,
                                    next_bank,
                                    tree->v.tree.bit_1,
                                    endianness);
        } else {
            next_read_huffman_state(state,
                                    next_bank,
                                    tree->v.tree.bit_0,
                                    endianness);

        }
    }
}

int total_non_leaf_nodes(struct huffman_node* tree) {
    if (tree->type == NODE_TREE) {
        return (1 +
                total_non_leaf_nodes(tree->v.tree.bit_0) +
                total_non_leaf_nodes(tree->v.tree.bit_1));
    } else
        return 0;
}

void transfer_huffman_tree(struct bs_huffman_table (*table)[][0x200],
                           struct huffman_node* tree) {
    int i;

    if (tree->type == NODE_TREE) {
        /*not sure if this can be made more efficient*/
        for (i = 0; i < 0x200; i++) {
            (*table)[tree->v.tree.id][i] = tree->v.tree.jump_table[i];
        }
        transfer_huffman_tree(table, tree->v.tree.bit_0);
        transfer_huffman_tree(table, tree->v.tree.bit_1);
    }
}

int compile_huffman_table(struct bs_huffman_table (**table)[][0x200],
                          struct huffman_frequency* frequencies,
                          bs_endianness endianness) {
    struct huffman_node* tree = build_huffman_tree(frequencies);
    int total_rows = compile_huffman_tree(table, tree, endianness);
    free_huffman_tree(tree);
    return total_rows;
}

#ifdef STANDALONE

#include <jansson.h>

struct huffman_frequency* json_to_frequencies(const char* path);

struct huffman_frequency parse_json_pair(json_t* bit_list, json_t* value);

int main(int argc, char* argv[]) {
    struct huffman_frequency* frequencies = json_to_frequencies(argv[1]);
    struct bs_huffman_table (*table)[][0x200];
    int row;
    int context;
    int total_rows;

    total_rows = compile_huffman_table(&table, frequencies, BS_BIG_ENDIAN);

    printf("{\n");
    for (row = 0; row < total_rows; row++) {
        printf("  {\n");

        for (context = 0; context < 0x200; context++) {
            printf("    {0x%X, %d}",
                   (*table)[row][context].context_node,
                   (*table)[row][context].value);
            if (context < (0x200 - 1))
                printf(",\n");
            else
                printf("\n");
        }
        if (row < (total_rows - 1))
            printf("  },\n");
        else
            printf("  }\n");
    }
    printf("}\n");

    free(table);
    free(frequencies);
    return 0;
}

struct huffman_frequency* json_to_frequencies(const char* path) {
    json_error_t error;
    json_t* input = json_load_file(path, 0, &error);
    size_t input_size;
    size_t i;
    int o;
    struct huffman_frequency* frequencies;

    if (input == NULL) {
        fprintf(stderr, "%s %d: %s\n", error.source, error.line, error.text);
        exit(1);
    }

    input_size = json_array_size(input);

    frequencies = malloc(sizeof(struct huffman_frequency) *
                         ((input_size / 2) + 1));

    for (i = o = 0; i < input_size; i += 2,o++) {
        frequencies[o] = parse_json_pair(json_array_get(input, i),
                                         json_array_get(input, i + 1));
    }

    /*add the terminator frequency*/
    frequencies[o].bits = 0;
    frequencies[o].length = 0;
    frequencies[o].value = 0;

    json_decref(input);

    return frequencies;
}

struct huffman_frequency parse_json_pair(json_t* bit_list, json_t* value) {
    struct huffman_frequency frequency;
    size_t i;

    frequency.bits = 0;
    frequency.length = 0;

    for (i = 0; i < json_array_size(bit_list); i++) {
        frequency.bits = ((frequency.bits << 1) |
                          json_integer_value(json_array_get(bit_list, i)));
        frequency.length++;
    }

    frequency.value = json_integer_value(value);

    return frequency;
}

#endif
