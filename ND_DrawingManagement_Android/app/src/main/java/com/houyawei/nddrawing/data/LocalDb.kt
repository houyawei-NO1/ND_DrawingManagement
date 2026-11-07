package com.houyawei.nddrawing.data

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import org.json.JSONObject
import java.io.File

class LocalDb(context: Context, dbFile: File) : SQLiteOpenHelper(context, dbFile.absolutePath, null, 1) {

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(
            """CREATE TABLE product(
                _id INTEGER PRIMARY KEY,
                category TEXT, material_code TEXT, name TEXT, model TEXT,
                hw_version TEXT, sw_version TEXT, remark TEXT, creator TEXT,
                created_at TEXT, updated_at TEXT, ext_json TEXT)"""
        )
        db.execSQL(
            """CREATE TABLE drawing(
                _id INTEGER PRIMARY KEY,
                product_id INTEGER, dtype INTEGER, format TEXT,
                drawing_date TEXT, created_at TEXT, rel_path TEXT,
                file_name TEXT, local_path TEXT)"""
        )
    }

    override fun onUpgrade(db: SQLiteDatabase, oldV: Int, newV: Int) {
        db.execSQL("DROP TABLE IF EXISTS product")
        db.execSQL("DROP TABLE IF EXISTS drawing")
        onCreate(db)
    }

    fun upsertProduct(p: Product) {
        val v = ContentValues().apply {
            put("_id", p.id)
            put("category", p.category)
            put("material_code", p.materialCode)
            put("name", p.name)
            put("model", p.model)
            put("hw_version", p.hwVersion)
            put("sw_version", p.swVersion)
            put("remark", p.remark)
            put("creator", p.creator)
            put("created_at", p.createdAt)
            put("updated_at", p.updatedAt)
            val obj = JSONObject()
            p.ext.forEach { (k, vv) -> obj.put(k, vv) }
            put("ext_json", obj.toString())
        }
        writableDatabase.insertWithOnConflict("product", null, v, SQLiteDatabase.CONFLICT_REPLACE)
    }

    fun upsertDrawing(d: Drawing) {
        val v = ContentValues().apply {
            put("_id", d.id)
            put("product_id", d.productId)
            put("dtype", d.dtype)
            put("format", d.format)
            put("drawing_date", d.drawingDate)
            put("created_at", d.createdAt)
            put("rel_path", d.relPath)
            put("file_name", d.fileName)
            put("local_path", d.localPath)
        }
        writableDatabase.insertWithOnConflict("drawing", null, v, SQLiteDatabase.CONFLICT_REPLACE)
    }

    // 仅同步数据库清单（该分类不下载文件）时调用：保留本地已下载文件的路径，
    // 避免取消某分类勾选后再同步，已下载图纸在详情页变成「文件未下载」。
    fun upsertDrawingKeepLocal(d: Drawing) {
        val cur = readableDatabase.query(
            "drawing", arrayOf("local_path"), "_id=?", arrayOf(d.id.toString()),
            null, null, null
        )
        val existing = if (cur.moveToFirst()) cur.getString(0) ?: "" else ""
        cur.close()
        upsertDrawing(if (existing.isNotEmpty()) d.copy(localPath = existing) else d)
    }

    fun clearAll() {
        writableDatabase.execSQL("DELETE FROM product")
        writableDatabase.execSQL("DELETE FROM drawing")
    }

    // 全量同步后清理服务端已不存在的图纸记录（如电脑上已删除的孤儿记录），
    // 避免详情页出现“图片未下载或文件缺失”的幽灵条目。
    fun pruneDrawingsExcept(ids: Set<Int>) {
        val db = writableDatabase
        if (ids.isEmpty()) {
            db.execSQL("DELETE FROM drawing")
            return
        }
        val placeholders = ids.joinToString(",") { "?" }
        val args: Array<Any> = ids.map { it as Any }.toTypedArray()
        db.execSQL("DELETE FROM drawing WHERE _id NOT IN ($placeholders)", args)
    }

    // 分类列表随 PC 端同步，存于 meta 表（运行时创建，避免升级数据库清空已同步数据）
    private fun ensureMeta() {
        writableDatabase.execSQL("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)")
    }

    fun saveCategories(list: List<String>) {
        ensureMeta()
        val arr = org.json.JSONArray()
        list.forEach { arr.put(it) }
        val v = ContentValues().apply {
            put("key", "categories")
            put("value", arr.toString())
        }
        writableDatabase.insertWithOnConflict("meta", null, v, SQLiteDatabase.CONFLICT_REPLACE)
    }

    // 字段配置也存于 meta 表，不需要升级数据库版本、不会清空已同步的产品与图纸
    fun saveFieldConfigs(list: List<FieldConfig>) {
        if (list.isEmpty()) return // 老服务端没有该接口时保留已有配置
        ensureMeta()
        val arr = org.json.JSONArray()
        list.forEach { f ->
            arr.put(JSONObject().apply {
                put("category", f.category)
                put("key", f.key)
                put("label", f.label)
                put("sort", f.sort)
            })
        }
        val v = ContentValues().apply {
            put("key", "field_configs")
            put("value", arr.toString())
        }
        writableDatabase.insertWithOnConflict("meta", null, v, SQLiteDatabase.CONFLICT_REPLACE)
    }

    // 取某分类的特有字段（按 PC 端 sort 排序）；未同步过配置时返回空列表，由 UI 走内置标签兜底
    fun fieldConfigs(category: String): List<FieldConfig> {
        return try {
            ensureMeta()
            val cur = readableDatabase.query(
                "meta", arrayOf("value"), "key=?", arrayOf("field_configs"), null, null, null
            )
            val all = cur.use {
                val out = mutableListOf<FieldConfig>()
                if (it.moveToFirst()) {
                    val arr = org.json.JSONArray(it.getString(0))
                    for (i in 0 until arr.length()) {
                        val o = arr.optJSONObject(i) ?: continue
                        out.add(
                            FieldConfig(
                                category = o.optString("category"),
                                key = o.optString("key"),
                                label = o.optString("label"),
                                sort = o.optInt("sort", 0)
                            )
                        )
                    }
                }
                out
            }
            all.filter { it.category == category && it.key.isNotEmpty() }.sortedBy { it.sort }
        } catch (e: Exception) {
            emptyList()
        }
    }

    fun getCategories(): List<String> {
        return try {
            ensureMeta()
            val cur = readableDatabase.query("meta", arrayOf("value"), "key=?", arrayOf("categories"), null, null, null)
            cur.use {
                if (it.moveToFirst()) {
                    val arr = org.json.JSONArray(it.getString(0))
                    val out = mutableListOf<String>()
                    for (i in 0 until arr.length()) out.add(arr.optString(i))
                    out
                } else emptyList()
            }
        } catch (e: Exception) { emptyList() }
    }

    fun allProducts(category: String, kw: String): List<Product> {
        val list = mutableListOf<Product>()
        val sel = mutableListOf<String>()
        val args = mutableListOf<String>()
        if (category.isNotEmpty()) { sel.add("category=?"); args.add(category) }
        if (kw.isNotEmpty()) {
            sel.add("(model LIKE ? OR material_code LIKE ? OR name LIKE ?)")
            args.add("%$kw%"); args.add("%$kw%"); args.add("%$kw%")
        }
        val cur = readableDatabase.query(
            "product", null,
            if (sel.isEmpty()) null else sel.joinToString(" AND "),
            if (args.isEmpty()) null else args.toTypedArray(),
            null, null, "model ASC"
        )
        cur.use {
            while (it.moveToNext()) {
                val ext = mutableMapOf<String, String>()
                val ej = it.getString(11) ?: ""
                if (ej.isNotEmpty()) {
                    val o = JSONObject(ej)
                    val keys = o.keys()
                    while (keys.hasNext()) {
                        val k = keys.next()
                        ext[k] = o.optString(k)
                    }
                }
                list.add(
                    Product(
                        id = it.getInt(0),
                        category = it.getString(1) ?: "",
                        materialCode = it.getString(2) ?: "",
                        name = it.getString(3) ?: "",
                        model = it.getString(4) ?: "",
                        hwVersion = it.getString(5) ?: "",
                        swVersion = it.getString(6) ?: "",
                        remark = it.getString(7) ?: "",
                        creator = it.getString(8) ?: "",
                        createdAt = it.getString(9) ?: "",
                        updatedAt = it.getString(10) ?: "",
                        ext = ext
                    )
                )
            }
        }
        return list
    }

    fun allDrawings(): List<Drawing> {
        val list = mutableListOf<Drawing>()
        val cur = readableDatabase.query("drawing", null, null, null, null, null, "_id ASC")
        cur.use {
            while (it.moveToNext()) {
                list.add(
                    Drawing(
                        id = it.getInt(0),
                        productId = it.getInt(1),
                        dtype = it.getInt(2),
                        format = it.getString(3) ?: "",
                        drawingDate = it.getString(4) ?: "",
                        createdAt = it.getString(5) ?: "",
                        relPath = it.getString(6) ?: "",
                        fileName = it.getString(7) ?: "",
                        localPath = it.getString(8) ?: ""
                    )
                )
            }
        }
        return list
    }

    // USB 导入后：按 relPath 重新计算每张图纸的本地路径（baseDir/relPath），文件缺失则清空
    fun rebuildLocalPaths(baseDir: File) {
        val cur = readableDatabase.query("drawing", arrayOf("_id", "rel_path"), null, null, null, null, null)
        val batch = mutableListOf<Pair<Int, String>>()
        cur.use {
            while (it.moveToNext()) {
                val id = it.getInt(0)
                val rel = it.getString(1) ?: ""
                val f = File(baseDir, rel)
                batch.add(id to if (f.exists()) f.absolutePath else "")
            }
        }
        val v = ContentValues()
        writableDatabase.beginTransaction()
        try {
            for ((id, lp) in batch) {
                v.clear()
                v.put("local_path", lp)
                writableDatabase.update("drawing", v, "_id=?", arrayOf(id.toString()))
            }
            writableDatabase.setTransactionSuccessful()
        } finally {
            writableDatabase.endTransaction()
        }
    }

    fun productById(id: Int): Product? = allProducts("", "").firstOrNull { it.id == id }

    // 按物料编码精确查找（二维码扫描后据此定位产品）
    fun productByMaterialCode(code: String): Product? {
        if (code.isEmpty()) return null
        val cur = readableDatabase.query(
            "product", null, "material_code=?", arrayOf(code),
            null, null, null
        )
        return cur.use {
            if (it.moveToFirst()) {
                val ext = mutableMapOf<String, String>()
                val ej = it.getString(11) ?: ""
                if (ej.isNotEmpty()) {
                    val o = JSONObject(ej)
                    val keys = o.keys()
                    while (keys.hasNext()) {
                        val k = keys.next()
                        ext[k] = o.optString(k)
                    }
                }
                Product(
                    id = it.getInt(0),
                    category = it.getString(1) ?: "",
                    materialCode = it.getString(2) ?: "",
                    name = it.getString(3) ?: "",
                    model = it.getString(4) ?: "",
                    hwVersion = it.getString(5) ?: "",
                    swVersion = it.getString(6) ?: "",
                    remark = it.getString(7) ?: "",
                    creator = it.getString(8) ?: "",
                    createdAt = it.getString(9) ?: "",
                    updatedAt = it.getString(10) ?: "",
                    ext = ext
                )
            } else null
        }
    }

    fun drawingsOf(productId: Int): List<Drawing> {
        val list = mutableListOf<Drawing>()
        val cur = readableDatabase.query(
            "drawing", null, "product_id=?", arrayOf(productId.toString()),
            null, null, "_id ASC"
        )
        cur.use {
            while (it.moveToNext()) {
                list.add(
                    Drawing(
                        id = it.getInt(0),
                        productId = it.getInt(1),
                        dtype = it.getInt(2),
                        format = it.getString(3) ?: "",
                        drawingDate = it.getString(4) ?: "",
                        createdAt = it.getString(5) ?: "",
                        relPath = it.getString(6) ?: "",
                        fileName = it.getString(7) ?: "",
                        localPath = it.getString(8) ?: ""
                    )
                )
            }
        }
        return list
    }
}
