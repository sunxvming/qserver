/////////////////////////////// DES //////////////////////////////
#include "openssl/des.h"

static int str_3desen( lua_State* lua )
{
	size_t keysize;
	const unsigned char* key = (unsigned char*) tobytes( lua, 1, &keysize, "#1" );
	unsigned char* key24 = NULL;
	if ( keysize < 24 )
	{
		key24 = (unsigned char*) malloc( 24 );
		if ( key24 == NULL )
		{
			error( lua, "key out of memory" );
			return 0;
		}

		memcpy( key24, key, keysize );
		memset( key24 + keysize, 0, 24 - keysize );
		key = key24;
	}

	size_t data_len;
	unsigned char* src = NULL;
	unsigned char* data = (unsigned char*) tobytes( lua, 2, &data_len, "#2" );
	int data_rest = data_len & 0x7;
	size_t len = 0;
	if ( data_rest == 0 )
	{
		src = data;
		len = data_len;
	}
	else
	{
		len = data_len + ( 8 - data_rest );
		unsigned char ch = 8 - data_rest;
		src = (unsigned char*) malloc( len );
		if ( src == NULL )
		{
			if ( key24 != NULL )
				free( key24 );

			error( lua, "src out of memory" );
			return 0;
		}

		memset( src, 0, len );
		memcpy( src, data, data_len );
		memset( src + data_len, ch, 8 - data_rest );
	}

	unsigned char* dst = (unsigned char*) malloc( len );
	if ( dst == NULL )
	{
		if ( src != data )
			free( src );
		if ( key24 != NULL )
			free( key24 );

		error( lua, "dst out of memory" );
		return 0;
	}

	unsigned char block_key[9];
	memset( block_key, 0, sizeof( block_key ) );

	DES_key_schedule ks, ks2, ks3;
	memcpy( block_key, key + 0, 8 );
	DES_set_key_unchecked( (const_DES_cblock*) block_key, &ks );

	memcpy( block_key, key + 8, 8 );
	DES_set_key_unchecked( (const_DES_cblock*) block_key, &ks2 );

	memcpy( block_key, key + 16, 8 );
	DES_set_key_unchecked( (const_DES_cblock*) block_key, &ks3 );

	for ( unsigned int i = 0; i < len >> 3; i ++ )
		DES_ecb3_encrypt( (const_DES_cblock*) ( src + ( i << 3 ) ), (DES_cblock*) ( dst + ( i << 3 ) ), &ks, &ks2, &ks3, DES_ENCRYPT );

	pushsl( lua, (char*) dst, len );

	if ( src != data )
		free( src );

	if ( key24 != NULL )
		free( key24 );

	free( dst );
	return 1;
}

static int str_3desde( lua_State* lua )
{
	size_t keysize;
	const unsigned char* key = (unsigned char*) tobytes( lua, 1, &keysize, "#1" );
	unsigned char* key24 = NULL;
	if ( keysize < 24 )
	{
		key24 = (unsigned char*) malloc( 24 );
		if ( key24 == NULL )
		{
			error( lua, "key out of memory" );
			return 0;
		}

		memcpy( key24, key, keysize );
		memset( key24 + keysize, 0, 24 - keysize );
		key = key24;
	}

	size_t len;
	const unsigned char* src = (unsigned char*) tobytes( lua, 2, &len, "#2" );
	if ( ( len & 0x7 ) != 0 )
	{
		if ( key24 != NULL )
			free( key24 );

		error( lua, "data length is not correct" );
		return 0;
	}

	unsigned char* dst = (unsigned char*) malloc( len );
	if ( dst == NULL )
	{
		if ( key24 != NULL )
			free( key24 );

		error( lua, "out of memory" );
		return 0;
	}

	unsigned char block_key[9];
	memset( block_key, 0, sizeof( block_key ) );

	DES_key_schedule ks, ks2, ks3;
	memcpy( block_key, key + 0, 8 );
	DES_set_key_unchecked( (const_DES_cblock*) block_key, &ks );

	memcpy( block_key, key + 8, 8 );
	DES_set_key_unchecked( (const_DES_cblock*) block_key, &ks2 );

	memcpy( block_key, key + 16, 8 );
	DES_set_key_unchecked( (const_DES_cblock*) block_key, &ks3 );

	for ( unsigned int i = 0; i < len >> 3; i ++ )
		DES_ecb3_encrypt( (const_DES_cblock*) ( src + ( i << 3 ) ), (DES_cblock*) ( dst + ( i << 3 ) ), &ks, &ks2, &ks3, DES_DECRYPT );

	int pad = dst[ len -1 ];
	if ( pad < 8 && pad > 1 )
	{
		if ( pad != dst[ len -2 ] )
			error( lua, "data corrupted" );

		len -= pad;
	}
	else if ( pad == 1 )
	{
		len --;
	}

	pushsl( lua, (char*) dst, len );

	if ( key24 != NULL )
		free( key24 );

	free( dst );
	return 1;
}
