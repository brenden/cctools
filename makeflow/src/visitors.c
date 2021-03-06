/*
Copyright (C) 2013- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hash_table.h"
#include "xxmalloc.h"
#include "list.h"
#include "stringtools.h"

#include "visitors.h"

/*
 * BUG: Error handling is not very good.
 * BUG: Integrate more with dttools (use DEBUG, etc.)
 */

/* Writes 'var=value' pairs from the dag to the stream */
int dag_to_file_vars(const struct dag *d, FILE *dag_stream)
{
	char *var;
	void *value;

	struct hash_table *vars = d->variables;

	hash_table_firstkey(vars);
	while(hash_table_nextkey(vars, &var, &value)) {
		if(!string_null_or_empty(value) && strcmp(var, "_MAKEFLOW_COLLECT_LIST"))
			fprintf(dag_stream, "%s=\"%s\"\n", var, (char *) value);
	}

	return 0;
}

/* Writes 'export var' tokens from the dag to the stream */
int dag_to_file_exports(const struct dag *d, FILE *dag_stream)
{
	char *var;

	struct list *vars = d->export_list;

	list_first_item(vars);
	for(var = list_next_item(vars);  var; var = list_next_item(vars))
		fprintf(dag_stream, "export %s\n", var);

	return 0;

}

/* Writes a list of files to the the stream */
int dag_to_file_files(struct dag_node *n, struct list *fs, FILE *dag_stream, char *(*rename)(struct dag_node *n, const char *filename))
{
	//here we may want to call the linker renaming function,
	//instead of using f->remotename
	
	const struct dag_file *f;
	list_first_item(fs);
	while( (f = list_next_item(fs)) )
		if(rename)
			fprintf(dag_stream, "%s ", rename(n, f->filename));
		else
		{
			char *remotename = dag_file_remote_name(n, f->filename); 
			if(remotename)
				fprintf(dag_stream, "%s->%s ", f->filename, remotename); 
			else
				fprintf(dag_stream, "%s ", f->filename);
		}

	return 0;
}

/* Writes a production rule to the stream, using remotenames when
 * available.
 *
 * Eventually, we would like to pass a 'convert_name' function,
 * instead of using just the remotenames.
 *
 * BUG: Currently, expansions are writen instead of variables.
 *
 * The entry function is dag_to_file(dag, filename).
 * */
int dag_to_file_node(struct dag_node *n, FILE *dag_stream, char *(*rename)(struct dag_node *n, const char *filename))
{
	fprintf(dag_stream, "\n");
	dag_to_file_files(n, n->target_files, dag_stream, rename);
	fprintf(dag_stream, ": ");
	dag_to_file_files(n, n->source_files, dag_stream, rename);
	fprintf(dag_stream, "\n");
	if (n->local_job)
		fprintf(dag_stream, "\tLOCAL %s", n->command);
	else
		fprintf(dag_stream, "\t%s\n", n->command);
	fprintf(dag_stream, "\n");

	return 0;
}

/* Writes all the rules to the stream */
int dag_to_file_nodes(const struct dag *d, FILE *dag_stream, char *(*rename)(struct dag_node *n, const char *filename))
{
	struct dag_node *n;

	for(n = d->nodes;  n; n = n->next)
		dag_to_file_node(n, dag_stream, rename);

	return 0;
}

/* Entry point of the dag_to_file* functions. Writes a dag as an
 * equivalent makeflow file. */
int dag_to_file(const struct dag *d, const char *dag_file, char *(*rename)(struct dag_node *n, const char *filename))
{
	FILE *dag_stream = fopen(dag_file, "w");

	if(!dag_stream)
		return 1;

	dag_to_file_vars(d, dag_stream);
	dag_to_file_exports(d, dag_stream);
	dag_to_file_nodes(d, dag_stream, rename);

	fclose(dag_stream);

	return 0;
}


/* The following functions and structures are used to write a dot
 * file (graphviz) that shows the graphical presentation of the
 * workflow. */

struct dot_node {
	int id;
	int count;
	int print;
};

struct file_node {
	int id;
	char *name;
	double size;
};

void dag_to_dot(struct dag *d, int condense_display, int change_size)
{
	struct dag_node *n;
	struct dag_file *f;
	struct hash_table *h, *g;
	struct dot_node *t;

	struct file_node *e;

	struct stat st;
	const char *fn;

	char *name;
	char *label;

        double average = 0;
        double width = 0;

        fprintf(stdout, "digraph {\n");

	if (change_size){
		hash_table_firstkey(d->completed_files);
		while(hash_table_nextkey(d->completed_files, &label, (void**)&name)) {
			stat(label, &st);
			average+=((double) st.st_size)/((double) hash_table_size(d->completed_files));
		}
	}
		
	
	h = hash_table_create(0,0);

	fprintf(stdout, "node [shape=ellipse,color = green,style = unfilled,fixedsize = false];\n");

	for(n = d->nodes; n; n = n->next){
		name = xxstrdup(n->command);
		label = strtok(name, " \t\n");
		t = hash_table_lookup(h, label);
		if (!t) {
			t = malloc(sizeof(*t));
			t->id = n->nodeid;
			t->count = 1;
			t->print = 1;
			hash_table_insert(h, label, t);
		} else {
			t->count++;
		}
		
		free(name);
	}
	

	for(n = d->nodes; n; n = n->next) {
        	name = xxstrdup(n->command);
		label = strtok(name, " \t\n");
		t = hash_table_lookup(h, label);
		if(!condense_display || t->print){

			if((t->count == 1) || !condense_display) fprintf(stdout, "N%d [label=\"%s\"];\n", condense_display?t->id:n->nodeid, label);
			else fprintf(stdout, "N%d [label=\"%s x%d\"];\n", t->id, label, t->count);
			t->print = 0;
		}
		free(name);
	}

	fprintf(stdout, "node [shape=box,color=blue,style=unfilled,fixedsize=false];\n");

	g = hash_table_create(0,0);

	for(n = d->nodes; n; n = n->next) {
		list_first_item(n->source_files);
		while ( (f = list_next_item(n->source_files)) ) {
			fn = f->filename;
			e = hash_table_lookup(g, fn);
			if (!e) {
				e = malloc(sizeof(*e));
				e->id = hash_table_size(g);
				e->name = xxstrdup(fn);
				if (stat(fn, &st) == 0) {
					e->size = (double)(st.st_size);	
				}
				else e->size = -1;
				hash_table_insert(g, fn, e);
			}
		}
		list_first_item(n->target_files);
		while ( (f = list_next_item(n->target_files)) ) {
			fn = f->filename;
                        e = hash_table_lookup(g, fn);
                        if (!e) {
                                e = malloc(sizeof(*e));
                                e->id = hash_table_size(g);
				e->name = xxstrdup(fn);
                                if (stat(fn, &st) == 0){
					e->size = (double)(st.st_size);
				}
                                else e->size = -1;
				hash_table_insert(g, fn, e);
                        }
                }
	}

	hash_table_firstkey(g);
        while(hash_table_nextkey(g, &label ,(void **)&e)) {
		fn = e->name;
		fprintf(stdout, "F%d [label = \"%s", e->id, fn);

		if (change_size) { 
			if (e->size >= 0){
				width = 5*(e->size/average);
				if (width <2.5) width = 2.5;
                                if (width >25) width = 25;
				fprintf(stdout, "\\nsize:%.0lfkb\", style=filled, fillcolor=skyblue1, fixedsize=true, width=%lf, height=0.75", e->size/1024, width);
			} else {
				fprintf(stdout, "\", fixedsize = false, style = unfilled, ");
			}
		} else fprintf(stdout, "\"");

		fprintf(stdout, "];\n");
				
        }

	fprintf(stdout, "\n");

	for(n =	d->nodes; n; n = n->next) {

		name = xxstrdup(n->command);
                label = strtok(name, " \t\n");
                t = hash_table_lookup(h, label);


		list_first_item(n->source_files);
		while ( (f = list_next_item(n->source_files)) ) {
			e = hash_table_lookup(g, f->filename);
			fprintf(stdout, "F%d -> N%d;\n", e->id, condense_display?t->id:n->nodeid);
		}

		list_first_item(n->target_files);
		while ( (f = list_next_item(n->target_files)) ) {
			e = hash_table_lookup(g, f->filename);
			fprintf(stdout, "N%d -> F%d;\n", condense_display?t->id:n->nodeid, e->id);
		}

		free(name);
	}

	fprintf(stdout, "}\n");

	hash_table_firstkey(h);
	while(hash_table_nextkey(h, &label ,(void **)&t)) {
		free(t);
		hash_table_remove(h, label);
	}

	hash_table_firstkey(g);
        while(hash_table_nextkey(g, &label ,(void **)&e)) {
                free(e);
                hash_table_remove(g, label);
        }

	hash_table_delete(g);
	hash_table_delete(h);
}

void dag_to_ppm (struct dag *d){

	int count, count_width, max_ancestor = 0, max_size = 0;
	UINT64_T key;
	struct dag_node *n;

        char *name;
        char *label;
	
	struct hash_table *h;

	dag_find_ancestor_depth(d);	

	h = hash_table_create(0, 0);

	itable_firstkey (d->node_table);
	while(itable_nextkey(d->node_table, &key, (void **)&n)) {

                name = xxstrdup(n->command);
                label = strtok(name, " \t\n");


		if (max_ancestor < n->ancestor_depth) max_ancestor = n->ancestor_depth;

		sprintf(name, "%d", n->nodeid);

		hash_table_insert(h, name, n);

	}

	struct list ** ancestor_count_list = malloc((max_ancestor+1)*sizeof(struct list*)); //pointer to a list of pointers

	//initialize all of the lists
	for(count = 0; count <= max_ancestor; count++){
		ancestor_count_list[count] = list_create();
	}

	hash_table_firstkey(h);
	while(hash_table_nextkey(h, &label, (void **)&n)){
		list_push_tail(ancestor_count_list[n->ancestor_depth], n);
		if (list_size(ancestor_count_list[n->ancestor_depth]) > max_size) max_size = list_size(ancestor_count_list[n->ancestor_depth]);
	}

	//Multiplier - get more pixels
	int multiplier = 20;
	//row and column multipliers, for screen size or file size based adjustments
	int row_multiplier = (800/multiplier)/max_ancestor;
	if(row_multiplier < 1) row_multiplier = 1;
	int column_multiplier = (800/multiplier)/max_size;
	if(column_multiplier < 1) column_multiplier = 1;
	int mult_count;

	//calculate the column size so that we can center the data
	
	int x_length = max_size*multiplier*column_multiplier; //x length
	int y_length = (max_ancestor+1)*multiplier*row_multiplier; //y length
	int ancestor_level_height = row_multiplier*multiplier; //y length of each line

	int white_space_left;
	int white_space_right;

	int current_ancestor_width;

	fprintf(stdout, "P3\n"); //"Magic Number", don't change
	fprintf(stdout, "%d %d\n", x_length, y_length); //Width and Height
	fprintf(stdout, "1\n"); //maximum color value

	int color_array[3];

	for(count = 0; count <= max_ancestor; count++){ //each ancestor depth in the dag

		//set the color of this particular depth
		memset(color_array, 0, 3*sizeof(int));
		color_array[count % 3] = 1;

		current_ancestor_width = list_size(ancestor_count_list[count]); //the width of this particular level of the dag
		current_ancestor_width = current_ancestor_width*multiplier*column_multiplier; //expands the current_ancestor width to its size in the image

		//set whitespace on left and right of this level's graphical output
		white_space_left = (x_length-current_ancestor_width)/2;
		white_space_right = x_length - white_space_left;

		for(mult_count = 0; mult_count < ancestor_level_height; mult_count++){ //each pixel row

			for (count_width = 0; count_width < x_length; count_width++){
				if ((count_width >= white_space_left) && (count_width <= white_space_right)) {
					fprintf(stdout, " %d %d %d ", color_array[0], color_array[1], color_array[2]);
				} else {
					fprintf(stdout, " 1 1 1 ");
				}
			}
			fprintf(stdout, "\n");
		}

	}

	hash_table_firstkey(h);
	//this doesn't cause a memory leak, right?
        while(hash_table_nextkey(h, &label ,(void **)&n)) {
                hash_table_remove(h, label);
        }

	hash_table_delete(h);
	free(ancestor_count_list);

}

