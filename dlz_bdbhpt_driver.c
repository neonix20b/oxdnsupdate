
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <db.h>
#include <pcre.h>


#define OVECCOUNT 30


/* should the bdb driver use threads. */
//#ifdef ISC_PLATFORM_USETHREADS
//#define bdbhpt_threads DB_THREAD
//#else
#define bdbhpt_threads 0
//#endif

/* bdbhpt database names */
#define dlz_data "dns_data"
#define dlz_zone "dns_zone"
#define dlz_xfr "dns_xfr"
#define dlz_client "dns_client"

	/* This structure contains all the Berkeley DB handles
	 * for this instance of the bdbhpt driver.
	 */

typedef struct bdbhpt_instance {
	DB_ENV	*dbenv;		/*%< bdbhpt environment */
	DB	*data;		/*%< dns_data database handle */
	DB	*zone;		/*%< zone database handle */
	DB	*xfr;		/*%< zone xfr database handle */
	DB	*client;	/*%< client database handle */

} bdbhpt_instance_t;

bdbhpt_instance_t *db = NULL;

int createFlag = 0;

char *bdbhpt_strrev(char *str)
{
	char *p1, *p2;

	if (! str || ! *str)
		return str;
	for (p1 = str, p2 = str + strlen(str) - 1; p2 > p1; ++p1, --p2)
	{
		*p1 ^= *p2;
		*p2 ^= *p1;
		*p1 ^= *p2;
	}
	return str;
}




/*%
 * Performs bdbhpt cleanup.
 * Used by bdbhpt_create if there is an error starting up.
 * Used by bdbhpt_destroy when the driver is shutting down.
 */

void bdbhpt_cleanup() {

	/* close databases */
	if (db->data != NULL)
		db->data->close(db->data, 0);
	if (db->xfr != NULL)
		db->xfr->close(db->xfr, 0);
	if (db->zone != NULL)
		db->zone->close(db->zone, 0);
	if (db->client != NULL)
		db->client->close(db->client, 0);

	/* close environment */
	if (db->dbenv != NULL)
		db->dbenv->close(db->dbenv, 0);

	/* cleanup memory */
	free(db);
}

/*% Initializes, sets flags and then opens Berkeley databases. */

int bdbhpt_opendb(DB_ENV *db_env, DBTYPE db_type, DB **db, const char *db_name,
	      char *db_file, int flags, int read) {

	int result;
	int openModeFlag;
	if(read)
		openModeFlag = DB_RDONLY;
	else
		openModeFlag = createFlag;//DB_CREATE;

	/* Initialize the database. */
	if ((result = db_create(db, db_env, 0)) != 0) {
		printf("bdbhpt could not initialize %s database. bdbhpt error: %s\n", db_name, db_strerror(result));
		return -1;
	}

	/* set database flags. */
	if ((result = (*db)->set_flags(*db, flags)) != 0) {
		printf("bdbhpt could not set flags for %s database. bdbhpt error: %s\n", db_name, db_strerror(result));
		return -1;
	}

	/* open the database. */
	if ((result = (*db)->open(*db, NULL, db_file, db_name, db_type,
				  openModeFlag | bdbhpt_threads, 0)) != 0) {
		printf("bdbhpt could not open %s database in %s. bdbhpt error: %s\n", db_name, db_file, db_strerror(result));
		return -1;
	}

	return 0;
}



int bdbhpt_create(unsigned int argc, char *argv[], int read)
{
	int result;
	int bdbhptres;
	int bdbFlags = 0;
	int openModeFlag;
	if(read)
		openModeFlag = 0;
	else
		openModeFlag = createFlag;//DB_CREATE;

	/* verify we have 4 arg's passed to the driver */
	if (argc != 4) {
		printf("bdbhpt driver requires at least 3 command line args.\n");
		return -1;
	}

	switch((char) *argv[1]) {
		/*
		 * Transactional mode.  Highest safety - lowest speed.
		 */
	case 'T':
	case 't':
		printf("dlzbdbhpt does not support transactional mode.\n");
		return -1;
		/*
		 * Concurrent mode.  Lower safety (no rollback) -
		 * higher speed.
		 */
	case 'C':
	case 'c':
		bdbFlags = DB_INIT_CDB | DB_INIT_MPOOL;
		printf("dlzbdbhpt using concurrent mode.\n");
		break;
		/*
		 * Private mode. No inter-process communication & no locking.
		 * Lowest saftey - highest speed.
		 */
	case 'P':
	case 'p':
		printf("dlzbdbhpt does not support private mode.\n");
		return -1;
	default:
		printf("bdbhpt driver requires the operating mode be set to P or C or T.  You specified '%s'\n", argv[1]);
		return -1;
	}

	/* allocate and zero memory for driver structure */
	db = (bdbhpt_instance_t*)malloc(sizeof(bdbhpt_instance_t));
	memset(db, 0, sizeof(bdbhpt_instance_t));

	/*
	 * create bdbhpt environment
	 * Basically bdbhpt allocates and assigns memory to db->dbenv
	 */
	bdbhptres = db_env_create(&db->dbenv, 0);
	if (bdbhptres != 0) {
		printf("bdbhpt environment could not be created. bdbhpt error: %s", db_strerror(bdbhptres));
		result = -1;
		goto init_cleanup;
	}

	/* open bdbhpt environment */
	bdbhptres = db->dbenv->open(db->dbenv, argv[2],
				    bdbFlags | bdbhpt_threads | openModeFlag, 0);
	if (bdbhptres != 0) {
		printf("bdbhpt environment at '%s' could not be opened. bdbhpt error: %s\n", argv[2], db_strerror(bdbhptres));
		result = -1;
		goto init_cleanup;
	}

	/* open dlz_data database. */
	result = bdbhpt_opendb(db->dbenv, DB_HASH, &db->data,
			       dlz_data, argv[3], DB_DUP | DB_DUPSORT, read);
	if (result != 0)
		goto init_cleanup;

	/* open dlz_xfr database. */
	result = bdbhpt_opendb(db->dbenv, DB_HASH, &db->xfr,
			       dlz_xfr, argv[3], DB_DUP | DB_DUPSORT, read);
	if (result != 0)
		goto init_cleanup;

	/* open dlz_zone database. */
	result = bdbhpt_opendb(db->dbenv, DB_BTREE, &db->zone,
			       dlz_zone, argv[3], 0, read);
	if (result != 0)
		goto init_cleanup;

	/* open dlz_client database. */
	result = bdbhpt_opendb(db->dbenv, DB_HASH, &db->client,
			       dlz_client, argv[3], DB_DUP | DB_DUPSORT, read);
	if (result != 0)
		goto init_cleanup;

	return 0;

 init_cleanup:

	bdbhpt_cleanup(db);
	return result;
}


void modify_db(const char* db_name, const char* operation, DBT* key, DBT* value) {
	DB* target_db = NULL;
	if(!strcmp(db_name,"D")) target_db = db->data;
	else if(!strcmp(db_name,"Z")) target_db = db->zone;
	else if(!strcmp(db_name,"X")) target_db = db->xfr;
	else if(!strcmp(db_name,"C")) target_db = db->client;
	if(!strcmp(operation,"+")) {
		target_db->put(target_db, NULL, key,value,0);
	} else if(!strcmp(operation,"-")) {
		DBC* cursor;
		target_db->cursor(target_db, NULL, &cursor, DB_WRITECURSOR);
		if(cursor->c_get(cursor,key,value,DB_GET_BOTH) != DB_NOTFOUND)
			cursor->c_del(cursor,0);
		cursor->c_close(cursor);
	} else if(!strcmp(operation,"~")) {
		DBC* cursor;
		target_db->cursor(target_db, NULL, &cursor, DB_WRITECURSOR);
		while(cursor->c_get(cursor,key,value,DB_NEXT) != DB_NOTFOUND)
			cursor->c_del(cursor,0);
		cursor->c_close(cursor);
	}
}


int parse_config(char* argv) {
	pcre *re;
	const char* error;
        int erroffset;
        int ovector[OVECCOUNT];
        const char* pattern = "^\\s*database\\s+\"bdbhpt\\s(.)\\s(.*?)\\s(.*?)\"\\s*;\\s*$";
        int rc;
	FILE* config;
	re = pcre_compile(pattern,0,&error,&erroffset,NULL);
        if (re == NULL) {
                printf("PCRE compilation failed at offset %d: %s\n", erroffset, error);
                return -1;
        }
	config = fopen("/etc/named/named.conf","rt");
	if(!config) {
		printf("failed to open /etc/named/named.conf\n");
		return -1;
	}
	char buf[1024];
	while(fgets(buf,1024,config) != 0) {
		rc = pcre_exec(re,NULL,buf,strlen(buf),0,0,ovector,OVECCOUNT);
		if(rc == 4) {			
			pcre_copy_substring(buf,ovector,rc,1,argv+1*128,128);
			pcre_copy_substring(buf,ovector,rc,2,argv+2*128,128);
			pcre_copy_substring(buf,ovector,rc,3,argv+3*128,128);
			fclose(config);
			return 0;
		}
	}
	fclose(config);
	printf("bdbhpt database line not found in /etc/named/named.conf\n");
	return -1;
}


void flush_db() {
}

int main(int argc, char* argv[]) {
	char buf[512] = {0};
	if(parse_config(buf)) {
		printf("Failed to parse bind config\n");
		return -1;
	}
	char* params[4];
	params[0]=buf+0*128;
	params[1]=buf+1*128;
	params[2]=buf+2*128;
	params[3]=buf+3*128;
	strncat(params[0], params[2], 128);
	strncat(params[0], "/", 128);
	strncat(params[0], params[3], 128);
	FILE* test = fopen(params[0],"r");
	if(!test) {
		createFlag = DB_CREATE;	
	} else
		fclose(test);
	if(bdbhpt_create(4, params,0)) {
		printf("Failed to configure BerkeleyDB\n");
		return -1;
	}
	pcre *re;
	const char* error;
	int erroffset;
	int ovector[OVECCOUNT];
	const char* pattern = "^([DZXC])([+-~])(?:(.+?)(?:=>(.+?))?)?$";
	int rc;
	re = pcre_compile(pattern,0,&error,&erroffset,NULL);
	if (re == NULL) {
		printf("PCRE compilation failed at offset %d: %s\n", erroffset, error);
		bdbhpt_cleanup();
		return 1;
	}
	while(fgets(buf,512,stdin) != 0 && strlen(buf)) {
		rc = pcre_exec(re,NULL,buf,strlen(buf),0,0,ovector,OVECCOUNT);
		if(rc != -1) {
			const char* dbs = 0;
			const char* act = 0;
			const char* key = 0;
			const char* val = 0;
			pcre_get_substring(buf,ovector,rc,1,&dbs);
			pcre_get_substring(buf,ovector,rc,2,&act);
			if(rc > 3)
				pcre_get_substring(buf,ovector,rc,3,&key);
			if(rc == 5)
				pcre_get_substring(buf,ovector,rc,4,&val);
			DBT db_key;
			memset(&db_key, 0, sizeof(DBT));
			if(key) {
				db_key.data = (void*)key;
				db_key.size = strlen(key);
			}
			DBT db_val;
			memset(&db_val, 0, sizeof(DBT));
			if(val) {
				db_val.data = (void*)val;
				db_val.size = strlen(val);
			}			
			modify_db(dbs,act,&db_key,&db_val);
			pcre_free_substring(dbs);
			pcre_free_substring(act);
			pcre_free_substring(key);
		}
	}
	bdbhpt_cleanup();
	return 0;
}

