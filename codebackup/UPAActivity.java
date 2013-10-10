package com.spreadtrum.android.eng;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;

import com.android.internal.telephony.Phone;
import com.android.internal.telephony.PhoneFactory;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.net.ConnectivityManager;
import android.net.NetworkInfo.State;
import android.os.Bundle;
import android.os.Handler;
import android.provider.Settings;
import android.telephony.TelephonyManager;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.View.OnFocusChangeListener;
import android.view.inputmethod.InputMethodManager;
import android.webkit.WebSettings;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

public class UPAActivity extends Activity {
    private static final String TAG = "upasetting";
    private int mSocketID = 0;
    private engfetch mEf;
    private EditText mEditor;
    private Button mBtnOk;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.upasetting);
        mEditor      = (EditText)findViewById(R.id.upa);
        mBtnOk       = (Button)findViewById(R.id.upasetting);
        initView();
//        selectCard();
    }

    private void initView(){
        mEditor.setFocusableInTouchMode(false);
        mEditor.clearFocus();
        InputMethodManager imm = (InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
        imm.hideSoftInputFromWindow(mEditor.getWindowToken(), 0);

        mEf = new engfetch();
        mSocketID = mEf.engopen();
        mEditor.append("Engmode socket open, id:"+mSocketID+"\n");

        mBtnOk.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                mEditor.setText("");
                selectCard();

                ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
                DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

                Log.d(TAG, "begin to  send the first cmd : at+cgeqreq=1,1,0,0");
                mEditor.append("begin to send the first cmd : at+cgeqreq=1,1,0,0"+"\n");
                String mATCgeqreq = new StringBuilder().append(engconstents.ENG_AT_NOHANDLE_CMD).append(",1,at+cgeqreq=1,1,0,0").toString();
                try {
                    outputBufferStream.writeBytes(mATCgeqreq);
                } catch (IOException e) {
                    Log.e(TAG, "writeBytes() error!");
                    return;
                }
                mEf.engwrite(mSocketID, outputBuffer.toByteArray(), outputBuffer.toByteArray().length);

                int dataSize = 128;
                byte[] inputBytes = new byte[dataSize];

                int showlen = mEf.engread(mSocketID, inputBytes, dataSize);
                String mATResponse1 = new String(inputBytes, 0, showlen);

                Log.d(TAG, "AT response:" + mATResponse1);
                mEditor.append("AT response:" + mATResponse1+"\n");
                try {
                    if (outputBufferStream != null) {
                        outputBufferStream.close();
                    }
                    if (outputBuffer != null) {
                        outputBuffer.close();
                    }
                } catch (IOException e) {
                    if(outputBufferStream!=null){
                        outputBufferStream=null;
                    }
                    if(outputBuffer!=null){
                        outputBuffer=null;
                    }
                }finally{
                    if(outputBufferStream!=null){
                        outputBufferStream=null;
                    }
                    if(outputBuffer!=null){
                        outputBuffer=null;
                    }
                }

                Log.d(TAG, "begin to send the second cmd : at+cgcmod=1");
                mEditor.append("begin to  send the second cmd : at+cgcmod=1"+"\n");

                ByteArrayOutputStream outputBuffer2 = new ByteArrayOutputStream();
                DataOutputStream outputBufferStream2 = new DataOutputStream(outputBuffer2);
                String mATCgcmod = new StringBuilder().append(engconstents.ENG_AT_NOHANDLE_CMD).append(",1,at+cgcmod=1").toString();
                try {
                    outputBufferStream2.writeBytes(mATCgcmod);
                } catch (IOException e) {
                    Log.e(TAG, "writeBytes() error!");
                    return;
                }
                mEf.engwrite(mSocketID, outputBuffer2.toByteArray(), outputBuffer2.toByteArray().length);

                byte[] inputBytes2 = new byte[dataSize];
                showlen = mEf.engread(mSocketID, inputBytes2, dataSize);
                String mATResponse2 = new String(inputBytes2, 0, showlen);
                Log.d(TAG, "AT response:" + mATResponse2);
                mEditor.append("AT response:" + mATResponse2+"\n\n");
                mEf.engclose(mSocketID);
                try {
                    if (outputBufferStream2 != null) {
                        outputBufferStream2.close();
                    }
                    if (outputBuffer2 != null) {
                        outputBuffer2.close();
                    }
                } catch (IOException e) {
                    if(outputBufferStream2!=null){
                        outputBufferStream2=null;
                    }
                    if(outputBuffer2!=null){
                        outputBuffer2=null;
                    }
                }finally{
                    if(outputBufferStream2!=null){
                        outputBufferStream2=null;
                    }
                    if(outputBuffer2!=null){
                        outputBuffer2=null;
                    }
                }
            }
        });
    }

    public void selectCard(){
        boolean isCard1Ready = TelephonyManager.getDefault(0).hasIccCard();
        boolean isCard2Ready = TelephonyManager.getDefault(1).hasIccCard();
        String mATCgeqreq;
        mEditor.append("begin to select card \n");
        if(isCard1Ready){
            mATCgeqreq = new StringBuilder().append(engconstents.ENG_AT_NOHANDLE_CMD).append(",1,AT+SPACTCARD=0").toString();
            mEditor.append("SIM Card1 is ready!! Select Card1 \n "+"AT+SPACTCARD=0\n");
        }else if (isCard2Ready) {
            mATCgeqreq = new StringBuilder().append(engconstents.ENG_AT_NOHANDLE_CMD).append(",1,AT+SPACTCARD=1").toString();
            mEditor.append("SIM Card2 is ready!! Select Card2 \n "+"AT+SPACTCARD=1\n");
        }else {
            mBtnOk.setEnabled(false);
            mEditor.append("NO SIM Card!! \n");
            return ;
        }

        ByteArrayOutputStream outputBuffer = new ByteArrayOutputStream();
        DataOutputStream outputBufferStream = new DataOutputStream(outputBuffer);

        try {
            outputBufferStream.writeBytes(mATCgeqreq);
        } catch (IOException e) {
            Log.e(TAG, "writeBytes() error!");
            return;
        }

        mEf.engwrite(mSocketID, outputBuffer.toByteArray(), outputBuffer.toByteArray().length);

        int dataSize = 128;
        byte[] inputBytes = new byte[dataSize];

        int showlen = mEf.engread(mSocketID, inputBytes, dataSize);
        String mATResponse1 = new String(inputBytes, 0, showlen);

        Log.d(TAG, "AT response:" + mATResponse1);
        mEditor.append("AT response:" + mATResponse1+"\n");
        try {
            if (outputBufferStream != null) {
                outputBufferStream.close();
            }
            if (outputBuffer != null) {
                outputBuffer.close();
            }
        } catch (IOException e) {
            if(outputBufferStream!=null){
                outputBufferStream=null;
            }
            if(outputBuffer!=null){
                outputBuffer=null;
            }
        }finally{
            if(outputBufferStream!=null){
                outputBufferStream=null;
            }
            if(outputBuffer!=null){
                outputBuffer=null;
            }
        }
    }

}
