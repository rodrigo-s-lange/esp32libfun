idf_component_register(SRCS "esp32libfun_at.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp32libfun_serial
                    WHOLE_ARCHIVE)
