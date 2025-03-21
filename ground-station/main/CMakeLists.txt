idf_component_register(SRCS "main.c"
                            "wifi.c"
                            "http.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "certs/servercert.pem"
                                   "certs/prvtkey.pem")
