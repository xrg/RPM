#include <sys/types.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <db.h>

DBT key;
DBT data;

int
main(int argc, char **argv)
{
  DB *db = 0;
  DBC *dbc = 0;
  int ret = 0;

  if (db_create(&db, 0, 0))
    {
      perror("db_create");
      exit(1);
    }
  if (db->open(db, 0, "/var/lib/rpm/Name", 0, DB_HASH, DB_RDONLY, 0664))
    {
      perror("db->open");
      exit(1);
    }
  if (argc == 1)
    {
      if (db->cursor(db, NULL, &dbc, 0))
	{
	  perror("db->cursor");
	  exit(1);
	}
      while (dbc->c_get(dbc, &key, &data, DB_NEXT) == 0)
	printf("%*.*s\n", (int)key.size, (int)key.size, (char *)key.data);
      dbc->c_close(dbc);
    }
  else
    {
      argc--;
      while (argc--)
	{
	  argv++;
	  key.data = (void *)*argv;
	  key.size = strlen(*argv);
	  data.data = NULL;
	  data.size = 0;
	  if (db->get(db, 0, &key, &data, 0) == 0)
	    printf("%s\n", *argv);
	  else
	    ret = 1;
	}
    }
  db->close(db, 0);
  return ret;
}
