/**
 * @file snapshot_config.h
 * @brief Konfigurace snapshotů — INI integrace
 */

#ifndef SNAPSHOT_CONFIG_H
#define SNAPSHOT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Registrace konfiguračních elementů do cfgmain systému
 * Volat z cfgmain_init() nebo z snapshot_init()
 */
void snapshot_config_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SNAPSHOT_CONFIG_H */
