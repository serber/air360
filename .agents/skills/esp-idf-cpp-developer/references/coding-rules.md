# Local rules
- No exceptions
- Prefer esp_err_t over bool for fallible operations
- Log through ESP_LOG*
- No blocking delays > 50 ms in shared tasks
- All new tasks must document stack size and priority