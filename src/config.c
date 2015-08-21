/*
 * File: config.c
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

#include "netmanage.h"
#include "config.h"

config_t *config;

#define LINE_MAX 1024

const config_t *config_get_section( const char *section )
{
	config_t *p = config;
	int match = -1;

	while( p && ((match = strncasecmp( p->section, section, LINE_MAX ))) < 0 )
		p = p->next;

	return match?0L:p;
}

const config_entry_t *config_get_entry( const config_t *section, const char *item )
{
	config_entry_t *e = section->config;
	int match = -1;

	while( e && ((match = strncasecmp( e->item, item, LINE_MAX ))) < 0 )
		e = e->next;

	return match?0L:e;
}

const char *config_get_item( const config_t *section, const char *item )
{
	if( section ) {
		const config_entry_t *e = config_get_entry( section, item );

		if( e )
			return e->value;
	}

	return 0L;
}

int config_istrue(const config_t * section, const char *item)
{
	const char     *i = config_get_item(section, item);

	if (i && *i) {
		if (!strcasecmp(i, "on"))
			return 1;
		if (!strcasecmp(i, "ok"))
			return 1;
		if ('o' == tolower(*i))
			if ('f' != tolower(i[1]))
				return 1;
		if ('t' == tolower(*i))
			return 1;
		if ('y' == tolower(*i))
			return 1;
		if (atoi(i) > 0)
			return 1;
	}
	return 0;
}

const char *config_item( const char *section, const char *item )
{
	return config_get_item( config_get_section( section ) , item );
}

const char **config_get_itemlist( const config_t *section, const char *item )
{
	if( section ) {
		const config_entry_t *e = config_get_entry( section, item );

		if( e )
			return e->list;
	}

	return 0L;
}

const char **config_itemlist( const char *section, const char *item )
{
	const config_t *s = config_get_section( section );

	return config_get_itemlist( s, item );
}

const char **add_config_item_list( config_entry_t *e, int linenr )
{
	if( e && e->value ) {
		unsigned int n = 0;
		int qt = 0;
		const char *p = e->value;

		// Count how many ','s or '|'s are present
		while(*p) {
			if( *p == ',' || *p == '|' )
				n++;
			p++;
		}

		// Allocate space for 'n' pointers, and the original string
		char **list = (char **) malloc( (n+2)*sizeof(char *) + strlen(e->value)+1 );

		// Install the string at the end of the buffer
		char *q = (char *) &list[n+2];
		strcpy( q, e->value );

		// Begin populating the pointer table with pointers into the string
		char *s;
		n = 0;

		// Overly complicated scanner, designed to split on ','s and absorb
		// leading and trailing spaces around the comma's.  Comma's in quotes aren't
		// split, but error checking around quotes is pretty slack.
		while( *q ) {
			s = 0L;
			while(isspace(*q))
				q++;

			if( *q == '"' || *q == '\'' ) {
				qt = *q++;
				list[n++] = q;
				while( *q && *q != qt )
					q++;
				if( ! *q )
					fprintf(stderr,"WARNING: Unterminated quote on line %d\n",linenr);
				else
					*q = ' ';
			} else
				list[n++] = q;

			while( *q && *q != ',' && *q != '|' ) {
				if( *q == ' ' ) {
					if( !s )
						s = q;
				} else {
					if( qt )
						fprintf(stderr,"WARNING: Garbage after quoted string on line %d\n",linenr);
					s = 0L;
				}
				q++;
			}

			if( s )
				*s = 0;
			if( *q )
				*q++ = 0;
		}

		// Terminate the table with a NULL pointer
		list[n++] = 0;
		return (const char **) list;
	}

	return 0;
}

config_t *add_config_section( const char *section )
{
	config_t **p = &config;
	int match = -1;

	while( *p && ((match = strncasecmp( (*p)->section, section, LINE_MAX ))) < 0 )
		p = &(*p)->next;

	if( match ) {
		config_t *s = (config_t *) calloc( 1, sizeof( config_t ) );
		s->section = strdup( section );
		s->next = (*p);
		(*p) = s;
	}

	return (*p);
}

config_entry_t *add_config_entry( config_t *section, const char *item, const char *value, int linenr )
{
	config_entry_t **e = &section->config;
	int match = -1;

	//printf("Adding config entry %s->%s = %s\n",section->section, item, value );

	while( (*e) && ((match = strncasecmp( (*e)->item, item, LINE_MAX ))) < 0 )
		e = &(*e)->next;

	if( match ) {
		config_entry_t *s = (config_entry_t *) calloc( 1, sizeof( config_entry_t ) );
		s->next = (*e);
		s->item = strdup( item );
		(*e) = s;
	}

	if( (*e)->value )
		free( (void *) (*e)->value );
	if( (*e)->list )
		free( (void *) (*e)->list );

	(*e)->value = strdup( value );
	(*e)->list = add_config_item_list( *e, linenr );

	return (*e);
}

void config_dump()
{
	d_printf("+++ Configuration Dump +++\n");
	config_t *p = config;
	while(p) {
		d_printf("[%s]\n",p->section);
		config_entry_t *e = p->config;
		while(e) {
			d_printf("%s=%s\n",e->item,e->value);
			e = e->next;
		}
		p = p->next;
	}
}

int config_read( FILE *fd )
{
	char buffer[LINE_MAX];
	char *section = 0L;
	int linenr = 0;
	char *bp;
	config_t *current = 0;

	while( fgets(buffer, sizeof( buffer ), fd ) ) {
		linenr++;
		bp = buffer;

		if( *bp == '#' || *bp == '\r' || *bp == '\n' || *bp == '\0' )
			continue;

		if( *bp == '[' ) {
			section = ++bp;
			if ((bp = strchr( section, ']' ))) {
				*bp = 0;
				current = add_config_section( section );
			} else {
				fprintf(stderr, "Error: malformed input at line %d\n",linenr);
				return -1;
			}
		} else {
			while(isspace(*bp))
				bp++;

			char *item = bp;
			char *sep = strchr( bp, '=' );
			if( sep ) {
				*sep++ = 0;
				char *value = sep;
				if((sep = strchr(value,'\n'))||(sep = strchr(value,'\r')))
					*sep = 0;
				add_config_entry( current, item, value, linenr );
			} else {
				fprintf(stderr,"Error: malformed input at line %d\n",linenr);
				return -1;
			}
		}
	}
	return 0;
}

int config_read_file( const char *file)
{
	int ret = 0;

	if( file ) {
		FILE *fd = fopen(file, "r");
		if( fd ) {
			ret = config_read( fd );
			fclose( fd );
		} else {
			ret = -1;
		}
	} else {
		ret = -1;
	}

	//config_dump();

	return ret;
}

void config_cleanup(void)
{
	config_t **pconfig = &config;

	while( *pconfig ) {
		config_t *tail = (*pconfig)->next;
		config_entry_t **pentry = & (*pconfig)->config;
		while( *pentry ) {
			config_entry_t *etail = (*pentry)->next;
			if( (*pentry)->item )
				free( (void *) (*pentry)->item );
			if( (*pentry)->value )
				free( (void *) (*pentry)->value );
			if( (*pentry)->list )
				free( (*pentry)->list );
			free( (void *) *pentry );
			*pentry = etail;
		}
		if( (*pconfig)->section )
			free( (void *)(*pconfig)->section );
		free( (void *) *pconfig );
		*pconfig = tail;
	}
}
