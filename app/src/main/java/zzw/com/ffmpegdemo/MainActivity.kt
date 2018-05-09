package zzw.com.ffmpegdemo

import android.Manifest
import android.support.v7.app.AppCompatActivity
import android.os.Bundle
import android.os.Environment
import android.support.v4.app.ActivityCompat
import android.util.Log
import android.view.View
import android.widget.Toast
import kotlinx.android.synthetic.main.activity_main.*
import java.io.File

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.WRITE_EXTERNAL_STORAGE), 5)
        VideoUtils.fix("11")

        sample_text.setOnClickListener {
            val parentDir = Environment.getExternalStorageDirectory().path + File.separator + "aaaaa" + File.separator
//            val srcFile = parentDir + "dst.yuv"
//            val destFile = parentDir + "dst.h264"
//            VideoUtils.encode(srcFile, destFile)

            val srcFile = parentDir + "music.pcm"
            val destFile = parentDir + "music.aac"
            VideoUtils.audio_encode(srcFile, destFile)

            Toast.makeText(this, "1111", Toast.LENGTH_SHORT).show()
        }
    }


}
