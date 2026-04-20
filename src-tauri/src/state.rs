use std::sync::Arc;

use anyhow::{Context, Result};
use tauri::{AppHandle, Manager};

use crate::bridge::Backend;

pub struct AppState {
    pub backend: Arc<Backend>,
}

impl AppState {
    pub fn new(app: &AppHandle) -> Result<Self> {
        let data_dir = app
            .path()
            .app_data_dir()
            .context("resolving app_data_dir")?;
        std::fs::create_dir_all(&data_dir).context("creating app_data_dir")?;
        let data_dir_str = data_dir
            .to_str()
            .context("app_data_dir is not valid UTF-8")?;

        let backend = Backend::new(data_dir_str)?;
        Ok(Self {
            backend: Arc::new(backend),
        })
    }
}
