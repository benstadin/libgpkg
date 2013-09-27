/*
 * Copyright 2013 Luciad (http://www.luciad.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "spatialdb_internal.h"
#include "gpkg_geom.h"
#include "sql.h"
#include "sqlite.h"

#define N NULL_VALUE
#define D(v) DOUBLE_VALUE(v)
#define I(v) INT_VALUE(v)
#define T(v) TEXT_VALUE(v)
#define F(v) FUNC_VALUE(v)

static column_info_t gpkg_spatial_ref_sys_columns[] = {
  {"srs_name", "text", N, SQL_NOT_NULL, NULL},
  {"srs_id", "integer", N, SQL_NOT_NULL | SQL_PRIMARY_KEY, NULL},
  {"organization", "text", N, SQL_NOT_NULL, NULL},
  {"organization_coordsys_id", "integer", N, SQL_NOT_NULL, NULL},
  {"definition", "text", N, SQL_NOT_NULL, NULL},
  {"description", "text", N, 0, NULL},
  {NULL, NULL, N, 0, NULL}
};
static value_t gpkg_spatial_ref_sys_data[] = {
  T("Undefined Cartesian"), I(-1), T("NONE"), I(-1), T("undefined"), N,
  T("Undefined Geographic"), I(0), T("NONE"), I(0), T("undefined"), N
};
static table_info_t gpkg_spatial_ref_sys = {
  "gpkg_spatial_ref_sys",
  gpkg_spatial_ref_sys_columns,
  gpkg_spatial_ref_sys_data, 2
};

static column_info_t gpkg_contents_columns[] = {
  {"table_name", "text", N, SQL_NOT_NULL | SQL_PRIMARY_KEY, NULL},
  {"data_type", "text", N, SQL_NOT_NULL, NULL},
  {"identifier", "text", N, 0, NULL},
  {"description", "text", T(""), 0, NULL},
  {"last_change", "text", F("strftime('%%Y-%%m-%%dT%%H:%%M:%%fZ', 'now')"), SQL_NOT_NULL, NULL},
  {"min_x", "double", N, 0, NULL},
  {"min_y", "double", N, 0, NULL},
  {"max_x", "double", N, 0, NULL},
  {"max_y", "double", N, 0, NULL},
  {"srs_id", "integer", N, 0, "CONSTRAINT fk_srid__gpkg_spatial_ref_sys_srs_id REFERENCES gpkg_spatial_ref_sys(srs_id)"},
  {NULL, NULL, N, 0, NULL}
};
static table_info_t gpkg_contents = {
  "gpkg_contents",
  gpkg_contents_columns,
  NULL, 0
};

static column_info_t gpkg_extensions_columns[] = {
  {"table_name", "text", N, SQL_UNIQUE(1), NULL},
  {"column_name", "text", N, SQL_UNIQUE(1), NULL},
  {"extension_name", "text", N, SQL_NOT_NULL | SQL_UNIQUE(1), NULL},
  {"scope", "text", N, SQL_NOT_NULL, NULL},
  {NULL, NULL, N, 0, NULL}
};
static table_info_t gpkg_extensions = {
  "gpkg_extensions",
  gpkg_extensions_columns,
  NULL, 0
};

static column_info_t gpkg_geometry_columns_columns[] = {
  {"table_name", "text", N, SQL_NOT_NULL | SQL_PRIMARY_KEY | SQL_UNIQUE(1),  "CONSTRAINT fk_table_name__gpkg_contents_table_name REFERENCES gpkg_contents(table_name)"},
  {"column_name", "text", N, SQL_NOT_NULL | SQL_PRIMARY_KEY, NULL},
  {"geometry_type", "text", N, SQL_NOT_NULL, NULL},
  {"srs_id", "integer", N, SQL_NOT_NULL, "CONSTRAINT fk_srs_id__gpkg_spatial_ref_sys_srs_id REFERENCES gpkg_spatial_ref_sys(srs_id)"},
  {"z", "integer", N, SQL_NOT_NULL, NULL},
  {"m", "integer", N, SQL_NOT_NULL, NULL},
  {NULL, NULL, N, 0, NULL}
};
static table_info_t gpkg_geometry_columns = {
  "gpkg_geometry_columns",
  gpkg_geometry_columns_columns,
  NULL, 0
};

static column_info_t gpkg_tile_matrix_metadata_columns[] = {
  {"table_name", "text", N, SQL_NOT_NULL | SQL_PRIMARY_KEY, "CONSTRAINT fk_table_name__gpkg_contents_table_name REFERENCES gpkg_contents(table_name)"},
  {"zoom_level", "integer", N, SQL_NOT_NULL | SQL_PRIMARY_KEY, NULL},
  {"matrix_width", "integer", N, SQL_NOT_NULL, NULL},
  {"matrix_height", "integer", N, SQL_NOT_NULL, NULL},
  {"tile_width", "integer", N, SQL_NOT_NULL, NULL},
  {"tile_height", "integer", N, SQL_NOT_NULL, NULL},
  {"pixel_x_size", "double", N, SQL_NOT_NULL, NULL},
  {"pixel_y_size", "double", N, SQL_NOT_NULL, NULL},
  {NULL, NULL, N, 0, NULL}
};
static table_info_t gpkg_tile_matrix_metadata = {
  "gpkg_tile_matrix_metadata",
  gpkg_tile_matrix_metadata_columns,
  NULL, 0
};

static const column_info_t gpkg_tiles_table_columns[] = {
  {"id", "integer", N, SQL_PRIMARY_KEY | SQL_AUTOINCREMENT, NULL},
  {"zoom_level", "integer", N, SQL_NOT_NULL | SQL_UNIQUE(1), NULL},
  {"tile_column", "integer", N, SQL_NOT_NULL | SQL_UNIQUE(1), NULL},
  {"tile_row", "integer", N, SQL_NOT_NULL | SQL_UNIQUE(1), NULL},
  {"tile_data", "blob", N, SQL_NOT_NULL, NULL},
  {NULL, NULL, N, 0, NULL}
};

static column_info_t gpkg_data_columns_columns[] = {
  {"table_name", "text", N, SQL_NOT_NULL | SQL_PRIMARY_KEY, "CONSTRAINT fk_table_name__gpkg_contents_table_name REFERENCES gpkg_contents(table_name)"},
  {"column_name", "text", N, SQL_NOT_NULL | SQL_PRIMARY_KEY, NULL},
  {"name", "text", N, 0, NULL},
  {"title", "text", N, 0, NULL},
  {"description", "text", N, 0, NULL},
  {"mime_type", "text", N, 0, NULL},
  {NULL, NULL, N, 0, NULL}
};
static table_info_t gpkg_data_columns = {
  "gpkg_data_columns",
  gpkg_data_columns_columns,
  NULL, 0
};

static column_info_t gpkg_metadata_columns[] = {
  {"id", "integer", N, SQL_NOT_NULL | SQL_AUTOINCREMENT | SQL_PRIMARY_KEY, NULL},
  {"md_scope", "text", T("dataset"), SQL_NOT_NULL, NULL},
  {"md_standard_uri", "text", T("http://schemas.opengis.net/iso/19139"), SQL_NOT_NULL, NULL},
  {"mime_type", "text", T("text/xml"), SQL_NOT_NULL, NULL},
  {"metadata", "text", T(""), SQL_NOT_NULL, NULL},
  {NULL, NULL, N, 0, NULL}
};
static table_info_t gpkg_metadata = {
  "gpkg_metadata",
  gpkg_metadata_columns,
  NULL, 0
};

static column_info_t gpkg_metadata_reference_columns[] = {
  {"reference_scope", "text", N, SQL_NOT_NULL, NULL},
  {"table_name", "text", N, 0, NULL},
  {"column_name", "text", N, 0, NULL},
  {"row_id_value", "integer", N, 0, NULL},
  {"timestamp", "text", F("strftime('%%Y-%%m-%%dT%%H:%%M:%%fZ', 'now')"), SQL_NOT_NULL, NULL},
  {"md_file_id", "integer", N, SQL_NOT_NULL, "CONSTRAINT fk_file_id__metadata_id REFERENCES gpkg_metadata(id)"},
  {"md_parent_id", "integer", N, 0, "CONSTRAINT fk_parent_id__metadata_id REFERENCES gpkg_metadata(id)"},
  {NULL, NULL, N, 0, NULL}
};
static table_info_t gpkg_metadata_reference = {
  "gpkg_metadata_reference",
  gpkg_metadata_reference_columns,
  NULL, 0
};

static const table_info_t *const gpkg_tables[] = {
  &gpkg_contents,
  &gpkg_extensions,
  &gpkg_spatial_ref_sys,
  &gpkg_data_columns,
  &gpkg_metadata,
  &gpkg_metadata_reference,
  &gpkg_geometry_columns,
  &gpkg_tile_matrix_metadata,
  NULL
};

static int init(sqlite3 *db, const char *db_name, error_t *error) {
  int result = SQLITE_OK;
  const table_info_t *const *table = gpkg_tables;

  while (*table != NULL) {
    result = sql_init_table(db, db_name, *table, error);
    if (result != SQLITE_OK) {
      break;
    }
    table++;
  }

  if (result == SQLITE_OK && error_count(error) > 0) {
    return SQLITE_ERROR;
  } else {
    return result;
  }
}

/*
 * Check that each 'features' table in gpkg_contents has at least one corresponding row in gpkg_geometry_columns.
 * This is not covered by a foreign key reference.
 */
static int gpkg_contents_geometry_table_check_row(sqlite3 *db, sqlite3_stmt *stmt, void *data) {
  error_append(
    (error_t *)data,
    "gpkg_contents: table '%s' has data_type 'features' but no rows exist in gpkg_geometry_columns for this table",
    sqlite3_column_text(stmt, 0)
  );
  return SQLITE_OK;
}

static int gpkg_contents_geometry_table_check(sqlite3 *db, const char *db_name, error_t *error) {
  return sql_exec_stmt(
           db, gpkg_contents_geometry_table_check_row, NULL, error,
           "SELECT table_name FROM \"%w\".gpkg_contents WHERE data_type='features' AND table_name NOT IN (SELECT table_name FROM \"%w\".gpkg_geometry_columns)",
           db_name, db_name
         );
}

/*
 * Check that each 'tiles' table in gpkg_contents has at least one corresponding row in gpkg_tile_matrix_metadata.
 * This is not covered by a foreign key reference.
 */
static int gpkg_contents_tilemetadata_table_check_row(sqlite3 *db, sqlite3_stmt *stmt, void *data) {
  error_append(
    (error_t *)data,
    "gpkg_contents: table '%s' has data_type 'tiles' but no rows exist in gpkg_tile_matrix_metadata for this table",
    sqlite3_column_text(stmt, 0)
  );
  return SQLITE_OK;
}

static int gpkg_contents_tilemetadata_table_check(sqlite3 *db, const char *db_name, error_t *error) {
  return sql_exec_stmt(
           db, gpkg_contents_tilemetadata_table_check_row, NULL, error,
           "SELECT table_name FROM \"%w\".gpkg_contents WHERE data_type='tiles' AND table_name NOT IN (SELECT table_name FROM \"%w\".gpkg_tile_matrix_metadata)",
           db_name, db_name
         );
}

/*
 * Check that each meta tables that reference columns all refer to tables and columns that actually exist.
 */
typedef struct {
  const char *db_name;
  const char *source_table;
  error_t *error;
} column_check_data_t;

static int gpkg_table_column_check_row(sqlite3 *db, sqlite3_stmt *stmt, void *data) {
  int result = SQLITE_OK;
  int exists = 0;
  column_check_data_t *c = (column_check_data_t *)data;
  char *table_name = sqlite3_mprintf("%s", sqlite3_column_text(stmt, 0));
  char *column_name;
  if (sqlite3_column_type(stmt, 1) == SQLITE_NULL) {
    column_name = NULL;
  } else {
    column_name = sqlite3_mprintf("%s", sqlite3_column_text(stmt, 1));
    if (column_name == NULL) {
      result = SQLITE_NOMEM;
      goto exit;
    }
  }


  result = sql_check_table_exists(db, c->db_name, table_name, &exists);
  if (result == SQLITE_OK && !exists) {
    error_append(c->error, "%s: table '%s' does not exist", c->source_table, table_name);
  }

  if (exists && column_name != NULL) {
    exists = 0;
    result = sql_check_column_exists(db, c->db_name, table_name, column_name, &exists);
    if (result == SQLITE_OK && !exists) {
      error_append(c->error, "%s: column '%s.%s' does not exist", c->source_table, table_name, column_name);
    }
  }

exit:
  sqlite3_free(table_name);
  sqlite3_free(column_name);
  return result;
}

static int gpkg_table_column_check(sqlite3 *db, const char *db_name, const char *table_name, const char *table_column, const char *column_column, error_t *error) {
  column_check_data_t c;
  c.db_name = db_name;
  c.source_table = table_name;
  c.error = error;

  if (column_column != NULL) {
    return sql_exec_stmt(db, gpkg_table_column_check_row, NULL, &c, "SELECT \"%w\", \"%w\" FROM \"%w\".\"%w\"", table_column, column_column, db_name, table_name);
  } else {
    return sql_exec_stmt(db, gpkg_table_column_check_row, NULL, &c, "SELECT \"%w\", NULL FROM \"%w\".\"%w\"", table_column, db_name, table_name);
  }
}

static int gpkg_contents_columns_table_column_check(sqlite3 *db, const char *db_name, error_t *error) {
  return gpkg_table_column_check(db, db_name, "gpkg_contents", "table_name", NULL, error);
}

static int gpkg_extensions_table_column_check(sqlite3 *db, const char *db_name, error_t *error) {
  return gpkg_table_column_check(db, db_name, "gpkg_extensions", "table_name", "column_name", error);
}

static int gpkg_geometry_columns_table_column_check(sqlite3 *db, const char *db_name, error_t *error) {
  return gpkg_table_column_check(db, db_name, "gpkg_geometry_columns", "table_name", "column_name", error);
}

static int gpkg_tile_matrix_metadata_table_column_check(sqlite3 *db, const char *db_name, error_t *error) {
  return gpkg_table_column_check(db, db_name, "gpkg_tile_matrix_metadata", "table_name", NULL, error);
}

static int gpkg_data_columns_table_column_check(sqlite3 *db, const char *db_name, error_t *error) {
  return gpkg_table_column_check(db, db_name, "gpkg_data_columns", "table_name", "column_name", error);
}

static int gpkg_metadata_reference_table_column_check(sqlite3 *db, const char *db_name, error_t *error) {
  return gpkg_table_column_check(db, db_name, "gpkg_metadata_reference", "table_name", "column_name", error);
}

typedef int(*check_func)(sqlite3 *db, const char *db_name, error_t *error);

static check_func checks[] = {
  gpkg_contents_columns_table_column_check,
  gpkg_extensions_table_column_check,
  gpkg_geometry_columns_table_column_check,
  gpkg_tile_matrix_metadata_table_column_check,
  gpkg_data_columns_table_column_check,
  gpkg_metadata_reference_table_column_check,
  gpkg_contents_geometry_table_check,
  gpkg_contents_tilemetadata_table_check,
  NULL
};

static int check(sqlite3 *db, const char *db_name, int detailed_check, error_t *error) {
  int result = SQLITE_OK;

  if (result == SQLITE_OK) {
    result = sql_check_table(db, db_name, &gpkg_contents, 1, error);
  }
  if (result == SQLITE_OK) {
    result = sql_check_table(db, db_name, &gpkg_spatial_ref_sys, 1, error);
  }
  if (result == SQLITE_OK) {
    result = sql_check_table(db, db_name, &gpkg_extensions, 0, error);
  }
  if (result == SQLITE_OK) {
    result = sql_check_table(db, db_name, &gpkg_data_columns, 0, error);
  }
  if (result == SQLITE_OK) {
    result = sql_check_table(db, db_name, &gpkg_metadata, 0, error);
  }
  if (result == SQLITE_OK) {
    result = sql_check_table(db, db_name, &gpkg_metadata_reference, 0, error);
  }
  if (result == SQLITE_OK) {
    int features = 0;
    result = sql_exec_for_int(db, &features, "SELECT count(*) FROM \"%w\".gpkg_contents WHERE data_type LIKE 'features'", db_name);
    if (result == SQLITE_OK) {
      result = sql_check_table(db, db_name, &gpkg_geometry_columns, features > 0, error);
    }
  }
  if (result == SQLITE_OK) {
    int tiles = 0;
    result = sql_exec_for_int(db, &tiles, "SELECT count(*) FROM \"%w\".gpkg_contents WHERE data_type LIKE 'tiles'", db_name);
    if (result == SQLITE_OK) {
      result = sql_check_table(db, db_name, &gpkg_tile_matrix_metadata, tiles > 0, error);
    }
  }

  if (detailed_check) {
    if (result == SQLITE_OK) {
      result = sql_check_integrity(db, db_name, error);
    }
    if (result == SQLITE_OK) {
      check_func *current_func = checks;
      while (*current_func != NULL) {
        result = (*current_func)(db, db_name, error);
        if (result != SQLITE_OK) {
          break;
        }
        current_func++;
      }
    }
  }

  return result;
}

static int write_blob_header(binstream_t *stream, geom_blob_header_t *header, error_t *error) {
  return gpb_write_header(stream, header, error);
}

static int read_blob_header(binstream_t *stream, geom_blob_header_t *header, error_t *error) {
  return gpb_read_header(stream, header, error);
}

static int gpkg_writer_init(geom_blob_writer_t *writer) {
  return gpb_writer_init(writer, -1);
}

static int add_geometry_column(sqlite3 *db, const char *db_name, const char *table_name, const char *column_name, const char *geom_type, int srs_id, int z, int m, error_t *error) {
  int result;

  const char *normalized_geom_type;
  result = geom_normalized_type_name(geom_type, &normalized_geom_type);
  if (result != SQLITE_OK) {
    error_append(error, "Invalid geometry type: %s", geom_type);
    return result;
  }

  if (z < 0 || z > 2) {
    error_append(error, "Invalid Z flag value: %d", z);
    return result;
  }

  if (m < 0 || m > 2) {
    error_append(error, "Invalid M flag value: %d", z);
    return result;
  }

  // Check if the target table exists
  int exists = 0;
  result = sql_check_table_exists(db, db_name, table_name, &exists);
  if (result != SQLITE_OK) {
    error_append(error, "Could not check if table %s.%s exists", db_name, table_name);
    return result;
  }

  if (!exists) {
    error_append(error, "Table %s.%s does not exist", db_name, table_name);
    return SQLITE_OK;
  }

  if (error_count(error) > 0) {
    return SQLITE_OK;
  }

  // Check if the SRID is defined
  int count = 0;
  result = sql_exec_for_int(db, &count, "SELECT count(*) FROM gpkg_spatial_ref_sys WHERE srs_id = %d", srs_id);
  if (result != SQLITE_OK) {
    return result;
  }

  if (count == 0) {
    error_append(error, "SRS %d does not exist", srs_id);
    return SQLITE_OK;
  }

  result = sql_exec(db, "ALTER TABLE \"%w\".\"%w\" ADD COLUMN \"%w\" %s", db_name, table_name, column_name, normalized_geom_type);
  if (result != SQLITE_OK) {
    error_append(error, sqlite3_errmsg(db));
    return result;
  }

  result = sql_exec(db, "INSERT INTO \"%w\".\"%w\" (table_name, column_name, geometry_type, srs_id, z, m) VALUES (%Q, %Q, %Q, %d, %d, %d)", db_name, "gpkg_geometry_columns", table_name, column_name,
                    normalized_geom_type, srs_id, z, m);
  if (result != SQLITE_OK) {
    error_append(error, sqlite3_errmsg(db));
    return result;
  }

  return SQLITE_OK;
}

static int create_tiles_table(sqlite3 *db, const char *db_name, const char *table_name, error_t *error) {
  int result = SQLITE_OK;

  // Check if the target table exists
  int exists = 0;
  result = sql_check_table_exists(db, db_name, table_name, &exists);
  if (result != SQLITE_OK) {
    error_append(error, "Could not check if table %s.%s exists", db_name, table_name);
    return result;
  }

  if (exists) {
    error_append(error, "Table %s.%s already exists", db_name, table_name);
    return SQLITE_OK;
  }

  table_info_t tile_table_info = {
    table_name,
    gpkg_tiles_table_columns,
    NULL, 0
  };

  // Check if required meta tables exist
  result = sql_init_table(db, db_name, &tile_table_info, error);
  if (result != SQLITE_OK) {
    return result;
  }

  return SQLITE_OK;
}

static int create_spatial_index(sqlite3 *db, const char *db_name, const char *table_name, const char *geometry_column_name, const char *id_column_name, error_t *error) {
  int result = SQLITE_OK;
  char *index_table_name = NULL;
  int exists = 0;

  index_table_name = sqlite3_mprintf("rtree_%s_%s", table_name, geometry_column_name);
  if (index_table_name == NULL) {
    result = SQLITE_NOMEM;
    goto exit;
  }

  // Check if the target table exists
  exists = 0;
  result = sql_check_table_exists(db, db_name, index_table_name, &exists);
  if (result != SQLITE_OK) {
    error_append(error, "Could not check if index table %s.%s exists: %s", db_name, index_table_name, sqlite3_errmsg(db));
    goto exit;
  }

  if (exists) {
    result = SQLITE_OK;
    goto exit;
  }

  // Check if the target table exists
  exists = 0;
  result = sql_check_table_exists(db, db_name, table_name, &exists);
  if (result != SQLITE_OK) {
    error_append(error, "Could not check if table %s.%s exists: %s", db_name, table_name, sqlite3_errmsg(db));
    goto exit;
  }

  if (!exists) {
    error_append(error, "Table %s.%s does not exist", db_name, table_name);
    goto exit;
  }

  int geom_col_count = 0;
  result = sql_exec_for_int(db, &geom_col_count, "SELECT count(*) FROM \"%w\".gpkg_geometry_columns WHERE table_name LIKE %Q AND column_name LIKE %Q", db_name, table_name, geometry_column_name);
  if (result != SQLITE_OK) {
    error_append(error, "Could not check if column %s.%s.%s exists in %s.gpkg_geometry_columns: %s", db_name, table_name, geometry_column_name, db_name, sqlite3_errmsg(db));
    goto exit;
  }

  if (geom_col_count == 0) {
    error_append(error, "Column %s.%s.%s is not registered in %s.gpkg_geometry_columns", db_name, table_name, geometry_column_name, db_name);
    goto exit;
  }

  result = sql_exec(db, "CREATE VIRTUAL TABLE \"%w\".\"%w\" USING rtree(id, minx, maxx, miny, maxy)", db_name, index_table_name);
  if (result != SQLITE_OK) {
    error_append(error, "Could not create rtree table %s.%s: %s", db_name, index_table_name, sqlite3_errmsg(db));
    goto exit;
  }

  result = sql_exec(
             db,
             "CREATE TRIGGER \"%w\".\"rtree_%w_%w_insert\" AFTER INSERT ON \"%w\"\n"
             "    WHEN (NEW.\"%w\" NOTNULL AND NOT ST_IsEmpty(NEW.\"%w\"))\n"
             "BEGIN\n"
             "  INSERT OR REPLACE INTO \"%w\" VALUES (\n"
             "    NEW.\"%w\",\n"
             "    ST_MinX(NEW.\"%w\"), ST_MaxX(NEW.\"%w\"),\n"
             "    ST_MinY(NEW.\"%w\"), ST_MaxY(NEW.\"%w\")\n"
             "  );\n"
             "END;",
             db_name, table_name, geometry_column_name, table_name,
             geometry_column_name, geometry_column_name,
             index_table_name,
             id_column_name,
             geometry_column_name, geometry_column_name,
             geometry_column_name, geometry_column_name
           );
  if (result != SQLITE_OK) {
    error_append(error, "Could not create rtree insert trigger: %s", sqlite3_errmsg(db));
    goto exit;
  }

  result = sql_exec(
             db,
             "CREATE TRIGGER \"%w\".\"rtree_%w_%w_update1\" AFTER UPDATE OF \"%w\" ON \"%w\"\n"
             "    WHEN OLD.\"%w\" = NEW.\"%w\" AND\n"
             "         (NEW.\"%w\" NOTNULL AND NOT ST_IsEmpty(NEW.\"%w\"))\n"
             "BEGIN\n"
             "  INSERT OR REPLACE INTO \"%w\" VALUES (\n"
             "    NEW.\"%w\",\n"
             "    ST_MinX(NEW.\"%w\"), ST_MaxX(NEW.\"%w\"),\n"
             "    ST_MinY(NEW.\"%w\"), ST_MaxY(NEW.\"%w\")\n"
             "  );\n"
             "END;",
             db_name, table_name, geometry_column_name, geometry_column_name, table_name,
             id_column_name, id_column_name,
             geometry_column_name, geometry_column_name,
             index_table_name,
             id_column_name,
             geometry_column_name, geometry_column_name,
             geometry_column_name, geometry_column_name
           );
  if (result != SQLITE_OK) {
    error_append(error, "Could not create rtree update trigger 1: %s", sqlite3_errmsg(db));
    goto exit;
  }

  result = sql_exec(
             db,
             "CREATE TRIGGER \"%w\".\"rtree_%w_%w_update2\" AFTER UPDATE OF \"%w\" ON \"%w\"\n"
             "    WHEN OLD.\"%w\" = NEW.\"%w\" AND\n"
             "         (NEW.\"%w\" ISNULL OR ST_IsEmpty(NEW.\"%w\"))\n"
             "BEGIN\n"
             "  DELETE FROM \"%w\" WHERE id = OLD.\"%w\";\n"
             "END;",
             db_name, table_name, geometry_column_name, geometry_column_name, table_name,
             id_column_name, id_column_name,
             geometry_column_name, geometry_column_name,
             index_table_name, id_column_name
           );
  if (result != SQLITE_OK) {
    error_append(error, "Could not create rtree update trigger 2: %s", sqlite3_errmsg(db));
    goto exit;
  }

  result = sql_exec(
             db,
             "CREATE TRIGGER \"%w\".\"rtree_%w_%w_update3\" AFTER UPDATE ON \"%w\"\n"
             "    WHEN OLD.\"%w\" != NEW.\"%w\" AND\n"
             "         (NEW.\"%w\" NOTNULL AND NOT ST_IsEmpty(NEW.\"%w\"))\n"
             "BEGIN\n"
             "  DELETE FROM \"%w\" WHERE id = OLD.\"%w\";\n"
             "  INSERT OR REPLACE INTO \"%w\" VALUES (\n"
             "    NEW.\"%w\",\n"
             "    ST_MinX(NEW.\"%w\"), ST_MaxX(NEW.\"%w\"),\n"
             "    ST_MinY(NEW.\"%w\"), ST_MaxY(NEW.\"%w\")\n"
             "  );\n"
             "END;",
             db_name, table_name, geometry_column_name, table_name,
             id_column_name, id_column_name,
             geometry_column_name, geometry_column_name,
             index_table_name,
             index_table_name,
             id_column_name,
             geometry_column_name, geometry_column_name,
             geometry_column_name, geometry_column_name
           );
  if (result != SQLITE_OK) {
    error_append(error, "Could not create rtree update trigger 3: %s", sqlite3_errmsg(db));
    goto exit;
  }

  result = sql_exec(
             db,
             "CREATE TRIGGER \"%w\".\"rtree_%w_%w_update4\" AFTER UPDATE ON \"%w\"\n"
             "    WHEN OLD.\"%w\" != NEW.\"%w\" AND\n"
             "         (NEW.\"%w\" ISNULL OR ST_IsEmpty(NEW.\"%w\"))\n"
             "BEGIN\n"
             "  DELETE FROM \"%w\" WHERE id IN (OLD.\"%w\", NEW.\"%w\");\n"
             "END;",
             db_name, table_name, geometry_column_name, table_name,
             id_column_name, id_column_name,
             geometry_column_name, geometry_column_name,
             index_table_name, id_column_name, id_column_name
           );
  if (result != SQLITE_OK) {
    error_append(error, "Could not create rtree update trigger 4: %s", sqlite3_errmsg(db));
    goto exit;
  }

  result = sql_exec(
             db,
             "CREATE TRIGGER \"%w\".\"rtree_%w_%w_delete\" AFTER DELETE ON \"%w\"\n"
             "BEGIN\n"
             "  DELETE FROM \"%w\" WHERE id = OLD.\"%w\";\n"
             "END;",
             db_name, table_name, geometry_column_name, table_name, geometry_column_name, geometry_column_name,
             index_table_name, id_column_name
           );
  if (result != SQLITE_OK) {
    error_append(error, "Could not create rtree delete trigger: %s", sqlite3_errmsg(db));
    goto exit;
  }

  result = sql_exec(
             db,
             "INSERT OR REPLACE INTO \"%w\".\"%w\" (id, minx, maxx, miny, maxy) "
             "  SELECT \"%w\", ST_MinX(\"%w\"), ST_MaxX(\"%w\"), ST_MinY(\"%w\"), ST_MaxY(\"%w\") FROM \"%w\".\"%w\""
             "  WHERE \"%w\" NOTNULL AND NOT ST_IsEmpty(\"%w\")",
             db_name, index_table_name,
             id_column_name, geometry_column_name, geometry_column_name, geometry_column_name, geometry_column_name, db_name, table_name,
             geometry_column_name, geometry_column_name
           );
  if (result != SQLITE_OK) {
    error_append(error, "Could not populate rtree: %s", sqlite3_errmsg(db));
    goto exit;
  }

  result = sql_exec(
             db,
             "INSERT OR REPLACE INTO \"%w\".\"gpkg_extensions\" (table_name, column_name, extension_name, scope) VALUES (\"%w\", \"%w\", \"%w\", \"%w\")",
             db_name, table_name, geometry_column_name, "gpkg_rtree_index", "write-only"
           );
  if (result != SQLITE_OK) {
    error_append(error, "Could not register rtree usage in gpkg_extensions: %s", sqlite3_errmsg(db));
    goto exit;
  }

exit:
  sqlite3_free(index_table_name);
  return result;
}

static int fill_envelope(binstream_t *stream, geom_envelope_t *envelope, error_t *error) {
  return wkb_fill_envelope(stream, WKB_ISO, envelope, error);
}

static int read_geometry_header(binstream_t *stream, geom_header_t *header, error_t *error) {
  return wkb_read_header(stream, WKB_ISO, header, error);
}

static int read_geometry(binstream_t *stream, geom_consumer_t const *consumer, error_t *error) {
  return wkb_read_geometry(stream, WKB_ISO, consumer, error);
}

static const spatialdb_t GEOPACKAGE = {
  "GeoPackage",
  NULL,
  init,
  check,
  write_blob_header,
  read_blob_header,
  gpkg_writer_init,
  gpb_writer_init,
  gpb_writer_destroy,
  add_geometry_column,
  create_tiles_table,
  create_spatial_index,
  fill_envelope,
  read_geometry_header,
  read_geometry
};

const spatialdb_t *spatialdb_geopackage_schema() {
  return &GEOPACKAGE;
}
